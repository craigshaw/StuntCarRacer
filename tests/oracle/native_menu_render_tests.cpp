#include "presentation/native_menu_render.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Memory=scr::runtime::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned stop{},pc{},instructions{};
std::uint64_t totalInstructions{},comparedPixels{};
bool invalid{},stopped{};
std::string failure;
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* why,unsigned address){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(address)+" pc="+std::to_string(pc);}
bool code(unsigned a){return in(a,0x64570,0x64652)||in(a,0x64b0e,0x64d08)||
    in(a,0x594c6,0x5982e)||in(a,0x5a64a,0x5a69a)||in(a,0x5b840,0x5b962)||
    in(a,0x64338,0x64354)||in(a,0x5df00,0x5df08)||in(a,0x6770a,0x6775a)||in(a,0x5e540,0x5e64e);}
void hook(unsigned address){
    pc=address;
    if(invalid){m68k_end_timeslice();return;}
    if(address==stop){stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
    if((address&1)||!code(address)||++instructions>2000000){fail("Unexpected PC or instruction budget",address);m68k_end_timeslice();}
}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    if(a<ram.size()&&(in(a,frame,frame+64000)||in(a,0x5982e,0x59845)||
        in(a,0x1bb00,0x1bc55)||a==0x64c18||a==0x64356||a==0x66475)) {ram[a]=v;return;}
    fail("Unexpected write",a);
}
void run(unsigned entry,unsigned boundary,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    invalid=stopped=false;failure.clear();instructions=0;stop=boundary;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(1000000000);
    if(invalid||!stopped)throw std::runtime_error("Original menu render failed: "+failure);
    totalInstructions+=instructions;
}
std::vector<std::uint8_t> readFile(const char* path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read local payload");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void compare(const scr::runtime::NativeMenuImage& image,const scr::runtime::NativeMenuRenderState& state){
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)
            if(ram[frame+p*8000+y*40+x/8]&(0x80u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(image.indices[y*320+x]!=expected)throw std::runtime_error("Native menu pixel mismatch x="+std::to_string(x)+" y="+std::to_string(y)+
            " offset="+std::to_string(state.menuOffset)+" selection="+std::to_string(state.selected)+
            " expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));
    }
}
void compareMessage(const std::vector<std::uint32_t>& pixels,const std::array<std::uint32_t,16>& palette,unsigned index,unsigned mode){
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)
            if(ram[frame+p*8000+y*40+x/8]&(0x80u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(pixels[y*320+x]!=palette[expected])throw std::runtime_error("Native game message mismatch x="+std::to_string(x)+" y="+std::to_string(y)+
            " index="+std::to_string(index)+" mode="+std::to_string(mode));
    }
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,static_cast<std::uint8_t>(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1){fail("Odd word write",a);return;}writeByte(a,static_cast<std::uint8_t>(v>>8));writeByte(a+1,static_cast<std::uint8_t>(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::invalid_argument("Usage: native_menu_render_tests original-payload.bin");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto base=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),base->begin()+0xe700);
    scr::runtime::initializeNativeAssets(*base);
    (*base)[0x1bbbc]=0x80;(*base)[0x5d724]=0x41;(*base)[0x1c9d0]=0;
    (*base)[0x1bb6d]=(*base)[0x1bb6e]=0;(*base)[0x64530]=0;(*base)[0x5eb76]=0;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    ram=*base;run(0x64570,0x645ac);
    compare(scr::runtime::makeNativeMenuBackground(*base),{});
    unsigned cases=1;
    for(unsigned offset:{0x10u,0u,0x18u,0x1cu})for(unsigned division=0;division<4;++division)
    for(unsigned selectedDivision=0;selectedDivision<(offset==0x1c?4u:1u);++selectedDivision)
    for(unsigned selection=0;selection<(offset==0x10||offset==0x1c?3u:4u);++selection){
        scr::runtime::NativeMenuRenderState state;
        state.menuOffset=offset;state.count=(offset==0x10||offset==0x1c)?2:3;
        state.selected=selection;state.division=division;state.selectedDivision=selectedDivision;state.menuStrings=offset==0x10||offset==0x18;
        ram=*base;ram[0x1c9ce]=static_cast<std::uint8_t>(division);ram[0x5eb7c]=static_cast<std::uint8_t>(selectedDivision);
        ram[0x5eb75]=offset==0x1c?0x40:(state.menuStrings?0x80:0);ram[0x64c18]=2;
        run(0x64570,0x645ac);run(0x5b840,0x5b962,selection,offset,state.count);
        compare(scr::runtime::makeNativeMenuImage(*base,state),state);++cases;
        // Confirmation repaints only the option strips; it retains the frame
        // and SELECT/header text from the preceding complete menu draw.
        ram[0x1bbca]=0x10;run(0x5b87e,0x5b962);state.confirmed=true;
        compare(scr::runtime::makeNativeMenuImage(*base,state),state);++cases;
    }
    const auto menuCases=cases;unsigned messageCases=0;
    const auto background=scr::runtime::makeNativeMenuBackground(*base);
    for(unsigned index:{0u,8u,0x1cu,0x2cu,0x3cu,0x4cu,0x54u,0x64u,0x74u,0x84u,0x94u,0xa4u,0xb4u,0xc4u,0xd4u})
    for(unsigned mode=1;mode<=4;++mode)for(unsigned blink=0;blink<2;++blink){
        ram=*base;run(0x64570,0x645ac);ram[0x5d724]=0x40;
        ram[0x5e65a]=static_cast<std::uint8_t>(mode);ram[0x5e65b]=static_cast<std::uint8_t>(index);
        ram[0x1bb88]=static_cast<std::uint8_t>(blink);
        run(0x5e540,0x5e64e);
        std::vector<std::uint32_t> pixels;scr::runtime::packNativeMenuPixels(background,pixels);
        scr::runtime::drawNativeGameMessage(*base,pixels,background.palette,index,mode,blink!=0);
        compareMessage(pixels,background.palette,index,mode);++cases;++messageCases;
    }
    std::cout<<"{\"mismatches\":0,\"total_cases\":"<<cases<<",\"menu_cases\":"<<menuCases<<",\"message_cases\":"<<messageCases<<",\"compared_pixels\":"<<comparedPixels<<
        ",\"original_instructions\":"<<totalInstructions<<",\"captured_state_used\":false,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

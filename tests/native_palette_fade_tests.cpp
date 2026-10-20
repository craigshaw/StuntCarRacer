#include "native_palette_fade.h"
#include "m68k.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace scr::viewer;
NativeMemory ram{};
std::array<std::uint8_t,4096> stack{};
std::vector<std::uint8_t> continuation;
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
enum class Phase{Copy,Fade,Interrupt};
Phase phase{};
unsigned pc{},instructions{};
std::uint64_t totalInstructions{},comparedValues{},vblanks{};
bool invalid{},yielded{},returned{},fadeFinished{};
std::string failure;
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
void hook(unsigned address){
    pc=address;
    if(invalid){m68k_end_timeslice();return;}
    if(address==sentinel){returned=true;if(phase==Phase::Fade)fadeFinished=true;m68k_end_timeslice();return;}
    if((phase==Phase::Interrupt&&address==0x61700)||
       (phase==Phase::Fade&&((address==0x64516&&ram[0x616d8])||(address==0x64524&&ram[0x616da])))){
        if(phase==Phase::Fade)m68k_get_context(continuation.data());
        yielded=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;
    }
    const bool allowed=phase==Phase::Interrupt?in(address,0x616e0,0x61700):
        in(address,0x1ba82,0x1bace)||(phase==Phase::Fade&&in(address,0x6444e,0x64530));
    if((address&1)||!allowed||++instructions>1000000){fail("Unexpected PC or instruction budget",address);m68k_end_timeslice();}
}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    if(in(a,0xe83a,0xe87a)||a==0x616d8||a==0x616da||
       (in(a,0xe770,0xe7b0)&&(a&3)<2)){ram[a]=v;return;}
    fail("Unexpected write",a);
}
unsigned word(unsigned a){return (unsigned(ram[a])<<8)|ram[a+1];}
void putWord(unsigned a,unsigned w){ram[a]=std::uint8_t(w>>8);ram[a+1]=std::uint8_t(w);}
void execute(){
    invalid=yielded=returned=false;failure.clear();instructions=0;m68k_execute(1000000000);
    if(invalid||(!yielded&&!returned))throw std::runtime_error("Original fade failed: "+failure);
    totalInstructions+=instructions;
}
void fresh(unsigned entry,Phase value,unsigned d0=0){
    phase=value;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_PC,entry);execute();
}
void originalVBlank(){
    fresh(0x616e0,Phase::Interrupt);++vblanks;
    if(!fadeFinished){phase=Phase::Fade;m68k_set_context(continuation.data());execute();}
}
void compare(const NativePaletteFade& fade){
    if(fade.active()==fadeFinished)throw std::runtime_error("Original/native fade completion differs");
    if(fade.stepCountdown()!=ram[0x616d8]||fade.minimumCountdown()!=ram[0x616da])
        throw std::runtime_error("Original/native fade vblank countdown differs");
    const auto rgb=fade.rgbPalette();
    for(unsigned i=0;i<16;++i){
        if(fade.current()[i]!=word(0xe83a+2*i)||fade.target()[i]!=word(0xe85a+2*i))
            throw std::runtime_error("Original/native source palette differs at index="+std::to_string(i));
        const auto copper=word(0xe770+4*i);
        const auto expected=(((copper>>8)&15)*17<<16)|(((copper>>4)&15)*17<<8)|((copper&15)*17);
        if(rgb[i]!=expected)throw std::runtime_error("Original/native copper palette differs at index="+std::to_string(i));
        comparedValues+=3;
    }
}
void runCase(const std::vector<std::uint8_t>& payload,const NativePalette& source,const NativePalette& target,bool flat){
    ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);
    for(unsigned i=0;i<16;++i){putWord(0xe83a+2*i,source[i]);putWord(0xe85a+2*i,source[i]);}
    ram[0x616d8]=ram[0x616da]=0;fresh(0x1ba82,Phase::Copy);
    NativePaletteFade fade;fade.reset(source);fadeFinished=false;
    if(flat){fade.fadeToColor(target[0]);fresh(0x6444e,Phase::Fade,target[0]);}
    else{
        for(unsigned i=0;i<16;++i)putWord(0xe85a+2*i,target[i]);
        fade.animateTo(target);fresh(0x64470,Phase::Fade);
    }
    compare(fade);
    for(unsigned frame=0;frame<32;++frame){originalVBlank();fade.advanceVBlank();compare(fade);}
    if(fade.active())throw std::runtime_error("Original fade exceeded its source palette duration");
}
std::vector<std::uint8_t> readFile(const char* path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read local payload");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1){fail("Odd word write",a);return;}writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_palette_fade_tests original-payload.bin");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    continuation.resize(m68k_context_size());unsigned cases=0;
    for(unsigned shift:{0u,4u,8u})for(unsigned from=0;from<8;++from)for(unsigned to=0;to<8;++to){
        NativePalette source{},target{};
        for(unsigned i=0;i<16;++i)source[i]=std::uint16_t(((i+from)&7)<<shift);
        target.fill(std::uint16_t(to<<shift));runCase(payload,source,target,true);++cases;
        runCase(payload,source,target,false);++cases;
    }
    auto memory=std::make_unique<NativeMemory>();std::copy(payload.begin(),payload.end(),memory->begin()+0xe700);
    for(unsigned address:{0x20184u,0x23c32u,0x2b974u}){
        const auto palette=readNativePalette(*memory,address);
        for(unsigned colour:{0u,0x22u}){
            NativePalette flat{};flat.fill(std::uint16_t(colour));
            runCase(payload,palette,flat,true);runCase(payload,flat,palette,false);cases+=2;
        }
    }
    // Original changes logical entry zero before looking for differences. If
    // the other fifteen entries already match, no copper write occurs at all.
    NativePalette source{},target{};target.fill(0x22);source=target;source[0]=0x777;
    runCase(payload,source,target,true);++cases;
    // Float presentation updates must reproduce exactly the same 50 Hz steps.
    NativePaletteFade whole,split;whole.reset({});split.reset({});target.fill(0x777);
    whole.animateTo(target);split.animateTo(target);whole.advanceVBlank(14);
    for(unsigned n=0;n<84;++n)split.advance(1.0/300);
    if(split.active()!=whole.active()||split.displayed()!=whole.displayed())throw std::runtime_error("Presentation delta changed PAL fade timing");
    bool rejected=false;try{split.advance(-1);}catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)throw std::runtime_error("Invalid fade delta accepted");
    std::cout<<"{\"mismatches\":0,\"total_cases\":"<<cases<<",\"compared_values\":"<<comparedValues<<
        ",\"original_instructions\":"<<totalInstructions<<",\"original_vblanks\":"<<vblanks<<
        ",\"captured_state_used\":false,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

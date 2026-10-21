#include "native_race_messages.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Memory=scr::viewer::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frameBase=0x6a594;
unsigned pc{},instructions{},gateCases{},pixelCases{};
std::uint64_t totalInstructions{},comparedPixels{};
bool invalid{},stopped{},gateOnly{},admitted{};
unsigned word(const Memory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const Memory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void putWord(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void putLong(Memory& m,unsigned a,unsigned v){putWord(m,a,v>>16);putWord(m,a+2,v);}
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    invalid=true;return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    if(!gateOnly&&a<ram.size()&&(in(a,frameBase,frameBase+64000)||
        in(a,0x5982e,0x59845)||in(a,0x1bb00,0x1bc55)||a==0x66475)){ram[a]=v;return;}
    invalid=true;
}
void hook(unsigned address){
    pc=address;
    if(address==sentinel||(gateOnly&&address==0x5e540)){
        admitted=address==0x5e540;stopped=true;
        if(admitted)m68k_set_reg(M68K_REG_PC,sentinel);
        m68k_end_timeslice();return;
    }
    const bool code=in(address,0x5e508,0x5e65a)||in(address,0x594c6,0x5982e)||
        in(address,0x5fae8,0x5faf2)||in(address,0x69f42,0x69fe8)||in(address,0x6770a,0x67740);
    if((address&1)||!code||++instructions>2000000){invalid=true;m68k_end_timeslice();}
}
bool run(unsigned entry,bool onlyGate=false){
    invalid=stopped=admitted=false;gateOnly=onlyGate;instructions=0;m68k_pulse_reset();stack.fill(0);
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(1000000000);
    if(invalid||!stopped)throw std::runtime_error("Original race message failed PC="+std::to_string(pc));
    totalInstructions+=instructions;return admitted;
}
std::vector<std::uint32_t> pixels(const Memory& m){
    const unsigned frame=longword(m,0x6a58c);std::vector<std::uint32_t> result(320*200);
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned colour=0;for(unsigned p=0;p<4;++p)
            colour|=((m[frame+p*8000+y*40+x/8]>>(7-x%8))&1)<<p;
        result[y*320+x]=colour;
    }return result;
}
void compare(const std::vector<std::uint32_t>& actual,unsigned variation){
    const auto expected=pixels(ram);
    for(unsigned i=0;i<actual.size();++i){
        ++comparedPixels;
        if(actual[i]!=expected[i])throw std::runtime_error("Race message pixel mismatch case="+
            std::to_string(variation)+" x="+std::to_string(i%320)+" y="+std::to_string(i/320));
    }++pixelCases;
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_race_messages_tests original-payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto source=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),source->begin()+0xe700);
    scr::viewer::initializeNativeAssets(*source);
    (*source)[0x5d724]=0x40;(*source)[0x5924a]=(*source)[0x59843]=(*source)[0x59844]=0;
    for(unsigned i=0;i<64000;++i)(*source)[frameBase+i]=std::uint8_t((i*37+(i>>7)*13)&255);
    auto assets=scr::viewer::decodeReplayRenderAssets(payload.data(),payload.size(),source->data(),source->size());
    for(unsigned i=0;i<assets.palette.size();++i)assets.palette[i]=i;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);

    // Exercise every countdown byte across both signed flag branches, including
    // noncanonical positive flags. The test stops original code at drawing entry.
    ram=*source;
    for(unsigned active:{0u,1u,127u,128u,255u})for(unsigned pause:{0u,1u,127u,128u,255u})
    for(unsigned index:{0x2cu,0x3cu})for(unsigned countdown=0;countdown<256;++countdown){
        ram[0x1bb8e]=std::uint8_t(active);ram[0x1bba4]=std::uint8_t(pause);
        ram[0x5e65b]=std::uint8_t(index);ram[0x1bb6c]=std::uint8_t(countdown);
        if(run(0x5e508,true)!=scr::viewer::captureNativeRaceMessage(ram).visible())
            throw std::runtime_error("Race message admission differs from original");
        ++gateCases;
    }

    const std::array<std::array<unsigned,2>,7> messages{{{{0,2}},{{8,4}},{{0x1c,4}},{{0x2c,4}},{{0x3c,4}},{{0x4c,2}},{{0x54,4}}}};
    for(const auto& message:messages)for(unsigned blink:{0u,1u,128u,255u})
    for(unsigned buffer=0;buffer<2;++buffer)for(unsigned restorePanel=0;restorePanel<2;++restorePanel){
        putLong(*source,0x6a58c,frameBase+buffer*32000);
        (*source)[0x1bb8e]=0x80;(*source)[0x1bba4]=(*source)[0x1bb6c]=0;
        (*source)[0x5e65b]=std::uint8_t(message[0]);(*source)[0x5e65a]=std::uint8_t(message[1]);
        (*source)[0x1bb88]=std::uint8_t(blink);ram=*source;
        const auto frame=scr::viewer::captureNativeRaceMessage(*source);auto actual=pixels(*source);
        if(frame.panel.x!=128||frame.panel.y!=27||frame.panel.widthWords!=4||frame.panel.height!=28)
            throw std::runtime_error("Original message panel placement changed");
        if(restorePanel)run(0x5fae8);
        run(0x5e508);
        const auto unchanged=std::make_unique<Memory>(*source);
        scr::viewer::drawNativeRaceMessage(*source,assets,frame,actual,restorePanel!=0);
        if(*source!=*unchanged)throw std::runtime_error("Race message drawing changed source RAM");
        compare(actual,pixelCases);
        // A retained frame supplies its own state even after current gameplay
        // has hidden or changed the message and moved the panel coordinates.
        (*source)[0x1bb8e]=0;(*source)[0x5e65b]=0;putWord(*source,0x6a49c+8,0);
        auto historical=pixels(*source);
        scr::viewer::drawNativeRaceMessage(*source,assets,frame,historical,restorePanel!=0);
        if(actual!=historical)throw std::runtime_error("Race message consumed later gameplay state");
        putWord(*source,0x6a49c+8,8);
    }
    {
        auto frame=scr::viewer::captureNativeRaceMessage(*source);frame.active=0;
        auto actual=pixels(*source);const auto unchanged=actual;
        scr::viewer::drawNativeRaceMessage(*source,assets,frame,actual,true);
        if(actual!=unchanged)throw std::runtime_error("Inactive race message changed pixels");
    }
    std::cout<<"{\"mismatches\":0,\"gate_cases\":"<<gateCases<<",\"pixel_cases\":"<<pixelCases
        <<",\"compared_pixels\":"<<comparedPixels<<",\"original_instructions\":"<<totalInstructions
        <<",\"original_code_unmodified\":true,\"captured_state_used\":false,\"source_unchanged\":true,\"retained_message_state_verified\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

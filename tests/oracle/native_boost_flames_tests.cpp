// The original flame branch and masked-object renderer execute unmodified.
#include "assets/native_startup.h"
#include "presentation/race_render_assets.h"
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
using namespace scr::runtime;
NativeMemory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stop=0xa0000,frameBase=0x6a594;
unsigned pc{},instructions{},cases{},phaseMask{};
std::uint64_t totalInstructions{},comparedPixels{};
std::vector<unsigned> originalObjects;
bool invalid{},returned{};
bool in(unsigned a,unsigned begin,unsigned end){return a>=begin&&a<end;}
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
void word(NativeMemory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(NativeMemory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
unsigned read(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,stop,stop+4))return (a&1)?0x71:0x4e;
    invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=std::uint8_t(v);return;}
    if(in(a,frameBase,frameBase+64000)||a==0x1bb69){ram[a]=std::uint8_t(v);return;}
    invalid=true;
}
void hook(unsigned address){
    pc=address;
    if(address==0x651b0){returned=true;m68k_set_reg(M68K_REG_PC,stop);m68k_end_timeslice();return;}
    if(invalid||!(in(address,0x6515c,0x651b0)||in(address,0x69f42,0x69fe8)||in(address,0x6770a,0x67740))||++instructions>100000){
        invalid=true;m68k_end_timeslice();return;
    }
    if(address==0x69f42)originalObjects.push_back(m68k_get_reg(nullptr,M68K_REG_D0)&255);
}
void run(){
    invalid=returned=false;instructions=0;originalObjects.clear();m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop);
    m68k_set_reg(M68K_REG_PC,0x6515c);m68k_execute(10000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)
        throw std::runtime_error("Original flame branch failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
std::vector<std::uint32_t> pixels(const NativeMemory& memory,unsigned base){
    const auto image=scr::decodePlanarImage(memory.data()+base,32000,320,200,4);
    return {image.pixels.begin(),image.pixels.end()};
}
void compare(const std::vector<std::uint32_t>& expected,const std::vector<std::uint32_t>& actual){
    comparedPixels+=expected.size();
    if(expected!=actual){const auto i=std::mismatch(expected.begin(),expected.end(),actual.begin()).first-expected.begin();
        throw std::runtime_error("Flame pixel differs case="+std::to_string(cases)+" pixel="+std::to_string(i));}
}
void step(CockpitFrame& frame,const RaceRenderAssets& assets,unsigned base){
    auto actual=pixels(ram,base),other=pixels(ram,frameBase+32000-(base-frameBase));
    frame.boostActive=ram[0x1bb62];advanceBoostFlames(frame.boostActive,frame.flameFrame);
    const auto before=std::make_unique<NativeMemory>(ram);
    composeBoostFlames(assets,frame,actual);
    if(ram!=*before)throw std::runtime_error("Flame rendering changed source memory");
    run();
    if(frame.flameFrame!=ram[0x1bb69])throw std::runtime_error("Flame phase differs from original");
    std::vector<unsigned> objects;
    if(frame.boostActive){const auto phase=frame.flameFrame;phaseMask|=1u<<phase;
        objects={phase==2?49u:phase+6u,phase==2?50u:phase+8u};}
    if(objects!=originalObjects)throw std::runtime_error("Flame draw order differs");
    compare(pixels(ram,base),actual);
    compare(pixels(ram,frameBase+32000-(base-frameBase)),other);
    // Repainting a completed frame must neither advance phase nor change it.
    auto again=pixels(*before,base);composeBoostFlames(assets,frame,again);
    if(again!=actual)throw std::runtime_error("Repeated flame drawing changed pixels");
    ++cases;
}
}
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}
unsigned m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_boost_flames_tests original-payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto source=std::make_unique<NativeMemory>();std::copy(payload.begin(),payload.end(),source->begin()+0xe700);
    initializeNativeAssets(*source);(*source)[0x5d724]=0x40;
    auto assets=decodeRaceRenderAssets(payload.data(),payload.size(),source->data(),source->size());
    for(unsigned i=0;i<16;++i)assets.palette[i]=i;
    for(unsigned i=0;i<64000;++i)(*source)[frameBase+i]=std::uint8_t(i*37+(i>>7)*13);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned buffer=0;buffer<2;++buffer)for(unsigned phase=0;phase<3;++phase)for(unsigned boost=0;boost<256;++boost){
        ram=*source;const unsigned base=frameBase+32000*buffer;longword(ram,0x6a58c,base);
        ram[0x1bb62]=std::uint8_t(boost);ram[0x1bb69]=std::uint8_t(phase);
        auto frame=captureCockpitFrame(ram.data(),ram.size());step(frame,assets,base);
    }
    // Keep independent original/native phases over presses, releases and resume.
    ram=*source;ram[0x1bb69]=0;longword(ram,0x6a58c,frameBase);
    auto frame=captureCockpitFrame(ram.data(),ram.size());
    for(unsigned i=0;i<96;++i){ram[0x1bb62]=(i%11<7)?0x80:0;step(frame,assets,frameBase);}
    if(phaseMask!=7)throw std::runtime_error("Not all flame images were drawn");
    std::cout<<"{\"mismatches\":0,\"cases\":"<<cases<<",\"phase_mask\":"<<phaseMask
        <<",\"compared_pixels\":"<<comparedPixels<<",\"original_instructions\":"<<totalInstructions
        <<",\"both_buffers_verified\":true,\"release_holds_phase\":true,\"render_queries_preserve_state\":true,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

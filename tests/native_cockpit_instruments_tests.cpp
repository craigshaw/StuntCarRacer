#include "../app/native_cockpit_instruments.h"
#include "../app/native_asset_initialization.h"
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
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},cases{};
unsigned stopAddress=sentinel;
std::uint64_t totalInstructions{},comparedBytes{};
bool invalid{},stopped{};
unsigned word(const Memory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const Memory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void word(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(Memory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;
    invalid=true;return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=v;return;}
    if(a<ram.size()){ram[a]=v;return;}
    invalid=true;
}
void hook(unsigned address){
    pc=address;
    if(address==stopAddress){stopped=true;m68k_end_timeslice();return;}
    if((address&1)||address<0xe700||address>=0x724dc||++instructions>2000000){invalid=true;m68k_end_timeslice();}
}
void run(unsigned entry,unsigned d0=0,unsigned d1=0,unsigned d2=0,unsigned boundary=sentinel){
    stopAddress=boundary;
    invalid=stopped=false;instructions=0;m68k_pulse_reset();stack.fill(0);
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original cockpit render failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
void compare(const Memory& actual,const char* stage,unsigned variation){
    const unsigned base=longword(ram,0x6a584);
    auto byte=[&](unsigned a){++comparedBytes;if(actual[a]!=ram[a])throw std::runtime_error(std::string(stage)+" mismatch variation="+
        std::to_string(variation)+" address="+std::to_string(a)+" expected="+std::to_string(ram[a])+" actual="+std::to_string(actual[a]));};
    for(unsigned a=base;a<base+64000;++a)byte(a);
    for(unsigned a:{0x1bb6d,0x1bb6e,0x1bb89,0x1bbe8,0x1bbd3,0x1bbde,0x1bb8c,0x1bb17,0x1bb18,0x1bb19,0x1bb1a})byte(a);
    for(unsigned a=0x5983e;a<=0x59844;++a)byte(a);
    for(unsigned a:{0x1bc18,0x1bc19,0x1bc20,0x1bc21})byte(a);
    ++cases;
}
void masks(Memory& m,unsigned paper,unsigned ink){
    for(unsigned p=0;p<4;++p){word(m,0x5982e+2*p,(paper&(1u<<p))?65535:0);word(m,0x59836+2*p,(ink&(1u<<p))?65535:0);}
}
void prepare(Memory& m,unsigned variation){
    m[0x5924a]=m[0x59843]=m[0x59844]=m[0x1bbd3]=0;m[0x1bbbc]=0;m[0x5d724]=0x80;
    m[0x1bb6d]=m[0x1bb6e]=0;word(m,0x1bc18,0);word(m,0x1bc20,0);
    const unsigned base=longword(m,0x6a584);
    for(unsigned i=0;i<64000;++i)m[base+i]=std::uint8_t((i*37+(i>>7)*13+variation*71)&255);
    masks(m,0,15);
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
    if(argc!=2)throw std::invalid_argument("Usage: native_cockpit_instruments_tests original-payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto base=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),base->begin()+0xe700);
    scr::viewer::initializeNativeAssets(*base);scr::viewer::initializeNativeGameData(*base);
    longword(*base,0x6a584,0x6a594);longword(*base,0x6a58c,0x6a594);prepare(*base,0);
    auto native=std::make_unique<Memory>();
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned v=0;v<95*16*4;++v){
        ram=*base;prepare(ram,v);const unsigned ch=32+v%95,offset=(v/95)%16,mode=v/(95*16);
        ram[0x5983e]=19;ram[0x5983f]=22;ram[0x1bb6d]=std::uint8_t(offset);
        ram[0x1bbd3]=(mode&1)?128:0;ram[0x1bbbc]=(mode&2)?128:0;
        if(mode&2)longword(ram,0x6a58c,0x6a594+32000);
        masks(ram,(v>>2)&15,(v*7)&15);*native=ram;
        run(0x594c6,ch);scr::viewer::drawNativeCockpitCharacter(*native,ch);compare(*native,"character",v);
    }
    for(unsigned v=0;v<512;++v){
        ram=*base;prepare(ram,v);const unsigned slot=v%24,pos=(v/24)%4,visibility=(v&1)?128:0;
        ram[0x1c908+slot]=std::uint8_t(v&15);ram[0x1c920+slot]=std::uint8_t(v*13);ram[0x1c938+slot]=std::uint8_t(v*29);
        masks(ram,v&15,(v+5)&15);*native=ram;run(0x5e326,visibility,pos,slot);
        scr::viewer::drawNativeCockpitLapTime(*native,visibility,pos,slot);compare(*native,"lap-time",v);
    }
    for(unsigned v=0;v<256;++v){
        ram=*base;prepare(ram,v);ram[0x1ca20]=std::uint8_t(v);*native=ram;run(0x60846);
        scr::viewer::drawNativeCockpitBoostDigits(*native);compare(*native,"boost",v);
    }
    for(unsigned v=0;v<1024;++v){
        ram=*base;prepare(ram,v);ram[0x1bbac]=std::uint8_t(v);ram[0x1bbb8]=(v&4)?128:0;
        ram[0x1bbaf]=(v&8)?4:0;word(ram,0x1bc38,std::uint16_t(v*67));word(ram,0x1bc3e,std::uint16_t(v*103));
        ram[0x1bb20]=std::uint8_t(v%4);ram[0x1bb21]=std::uint8_t((v/4)%4);
        ram[0x1bb1c]=std::uint8_t((v/16)%12);ram[0x1bb1d]=std::uint8_t((v/64)%12);ram[0x1ca1a]=12;ram[0x1ca32]=3;
        *native=ram;run(0x5e12a);scr::viewer::drawNativeCockpitDistance(*native);compare(*native,"distance",v);
    }
    for(unsigned v=0;v<4096;++v){
        ram=*base;prepare(ram,v);word(ram,0x1bd30,v*16);word(ram,0x1bc20,(v*37)%128);*native=ram;run(0x5e23e);
        scr::viewer::updateNativeCockpitSpeedometer(*native);compare(*native,"speed",v);
    }
    for(unsigned v=0;v<64;++v){
        ram=*base;prepare(ram,v);ram[0x1bbb2]=std::uint8_t(v*4);*native=ram;run(0x5e098);
        scr::viewer::drawNativeCockpitResultIcon(*native);compare(*native,"result-icon",v);
        ram=*base;prepare(ram,v);*native=ram;run(0x5e474);scr::viewer::drawNativeCockpitLabels(*native);compare(*native,"labels",v);
    }
    for(unsigned v=0;v<512;++v){
        ram=*base;prepare(ram,v);ram[0x1bb20]=std::uint8_t(v&3);ram[0x1bb21]=std::uint8_t((v>>2)&3);
        ram[0x1bb1c]=std::uint8_t((v>>4)%12);ram[0x1bb1d]=std::uint8_t((v>>6)%12);
        ram[0x1ca32]=3;ram[0x1ca1a]=12;word(ram,0x1bc3e,(v&256)?65535:0);ram[0x57c3c]=(v&128)?128:0;
        *native=ram;run(0x5e0c4);scr::viewer::drawNativeCockpitLeaderIcon(*native);compare(*native,"leader-icon",v);
    }
    for(unsigned v=0;v<64;++v){
        ram=*base;prepare(ram,v);ram[0x5983e]=std::uint8_t(20+v%24);ram[0x5983f]=23;ram[0x5d724]=0x41;
        ram[0x1bbd3]=(v&1)?128:0;*native=ram;const unsigned ch=(v&2)?127:'+';
        run(0x594c6,ch);scr::viewer::drawNativeCockpitCharacter(*native,ch);compare(*native,"displaced-erase",v);
    }
    for(unsigned v=0;v<64;++v){
        ram=*base;prepare(ram,v);*native=ram;
        const scr::PackedLapTime eventTime{std::uint8_t(v&15),std::uint8_t(v*13),std::uint8_t(v*29)};
        ram[0x1c908+2]=eventTime.minutes;ram[0x1c920+2]=eventTime.seconds;ram[0x1c938+2]=eventTime.subseconds;
        run(0x5e326,128,3,2);scr::viewer::drawNativeCockpitLapTime(*native,eventTime,128,3,2);
        compare(*native,"immutable-time-event",v);
        if((*native)[0x1c908+2]!=(*base)[0x1c908+2]||(*native)[0x1c920+2]!=(*base)[0x1c920+2]||(*native)[0x1c938+2]!=(*base)[0x1c938+2])
            throw std::runtime_error("Rendering a captured timer event changed the current clock slot");
    }
    for(unsigned v=0;v<32;++v){
        ram=*base;prepare(ram,v);*native=ram;
        run(0x5981a,14);run(0x59806,0);run(0x5e474);run(0x5e0d6,0,0,11);run(0x5e0aa,0,0,11);
        scr::viewer::initializeNativeCockpitInstruments(*native);compare(*native,"race-entry",v);
        for(unsigned a=0x5982e;a<0x5983e;++a)if((*native)[a]!=ram[a])throw std::runtime_error("Race-entry text colour mask mismatch");
    }
    for(unsigned v=0;v<16;++v){
        ram=*base;prepare(ram,v);ram[0x1bb20]=std::uint8_t(v);*native=ram;
        run(0x5dd86,0,0,0,0x5ddd0);scr::viewer::drawNativeCockpitLapCounter(*native);compare(*native,"lap-counter",v);
    }
    {
        *native=*base;ram=*base;prepare(*native,37);const auto preserved=std::make_unique<Memory>(*native);
        ram[0x1ca20]=0x17;ram[0x1bb20]=3;word(ram,0x1bd30,0x1234);const auto source=std::make_unique<Memory>(ram);
        scr::viewer::syncNativeCockpitInstrumentInputs(*native,ram);
        if(ram!=*source)throw std::runtime_error("Instrument input synchronization changed gameplay RAM");
        if((*native)[0x1ca20]!=0x17||(*native)[0x1bb20]!=3||word(*native,0x1bd30)!=0x1234)
            throw std::runtime_error("Instrument input synchronization lost gameplay inputs");
        const unsigned frame=longword(*native,0x6a584);
        if(!std::equal(native->begin()+frame,native->begin()+frame+64000,preserved->begin()+frame))
            throw std::runtime_error("Instrument input synchronization changed the rendered cockpit");
        for(unsigned a:{0x1bbde,0x1bb8c,0x1bc20,0x1bc21,0x5983e,0x5983f,0x1bb6d,0x1bb6e})
            if((*native)[a]!=(*preserved)[a])throw std::runtime_error("Instrument input synchronization changed renderer history");
    }
    std::cout<<"{\"cases\":"<<cases<<",\"compared_planar_and_state_bytes\":"<<comparedBytes<<",\"mismatches\":0,\"original_instructions\":"<<totalInstructions
        <<",\"original_code_unmodified\":true,\"both_buffers_verified\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

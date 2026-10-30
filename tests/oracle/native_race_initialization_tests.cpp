#include "race/native_race_initialization.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using Ram=scr::runtime::NativeMemory;
Ram original{},native{},baseline{};
std::array<std::uint8_t,4096> stackBytes{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned instructions=0,pc=0,maximumInstructions=0;
bool invalid=false,returned=false;
bool loadTransition=false;
std::uint64_t cases=0,compared=0;
std::uint32_t seed=0x53544152;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
unsigned read(unsigned address) {
    if(address<original.size())return original[address];
    if(address>=stackBase&&address<stackTop)return stackBytes[address-stackBase];
    if(address>=sentinel&&address<sentinel+4) {
        constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[address-sentinel];
    }
    if((address>=0xbfd000&&address<0xbff000)||(address>=0xdff000&&address<0xdff200))return 0;
    invalid=true;return 0;
}
void write(unsigned address,unsigned value) {
    if(address<original.size()){original[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stackBytes[address-stackBase]=std::uint8_t(value);return;}
    if((address>=0xbfd000&&address<0xbff000)||(address>=0xdff000&&address<0xdff200))return;
    invalid=true;
}
void hook(unsigned address) {
    pc=address;
    if(loadTransition&&address==0x5cf40){returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>1000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
void compare(const char* stage) {
    for(unsigned a=0;a<original.size();++a) {
        if(loadTransition&&a>=0x24&&a<0x28)continue; // Amiga exception vector only.
        ++compared;
        if(original[a]!=native[a]) {
            std::ostringstream error;error<<stage<<" case="<<cases<<" address=0x"<<std::hex<<a
                <<" original="<<unsigned(original[a])<<" native="<<unsigned(native[a]);
            throw std::runtime_error(error.str());
        }
    }
}
void run(unsigned entry,void(*routine)(Ram&),const char* stage) {
    loadTransition=entry==0x5c960;
    native=original;
    routine(native);
    instructions=0;invalid=returned=false;stackBytes.fill(0);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stackBytes[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(20000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=(loadTransition?stackTop-4:stackTop)) {
        std::ostringstream error;error<<"Original initializer failed at0x"<<std::hex<<pc<<" SP="<<m68k_get_reg(nullptr,M68K_REG_SP)
            <<" invalid="<<invalid<<" returned="<<returned;
        throw std::runtime_error(error.str());
    }
    compare(stage);maximumInstructions=std::max(maximumInstructions,instructions);++cases;
}
void prepare() {
    original=baseline;
    for(unsigned a=0x1baf8;a<0x1bdd0;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x1c908;a<0x1c950;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x1c230;a<0x1c278;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x1bdd0;a<0x1be10;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x3d80;a<0x6490;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0xead6;a<0xeb56;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x636b8;a<0x636bd;++a)original[a]=std::uint8_t(next());
    original[0x5eb79]=std::uint8_t(next()%12);original[0x1ca29]=std::uint8_t(next()%12);
    original[0x1c9cf]=std::uint8_t(next()%11);
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("Usage: native_race_initialization_tests payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read local original payload");
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned variation=0;variation<32;++variation){
        original=baseline;
        for(unsigned address:{0x64aecu,0x7a21au,0x7a416u,0x5cf2cu})
            for(unsigned i=0;i<4;++i)original[address+i]=std::uint8_t(next());
        original[0x1bb94]=std::uint8_t(next());original[0x64af0]=std::uint8_t(next());
        run(0x5c960,scr::runtime::initializeNativeLoadState,"successful-load-transition");
    }
    for(unsigned context:{0u,11u})for(unsigned network:{0u,0x40u})for(unsigned fuel:{0u,1u,2u,25u,50u,99u,100u,255u})for(unsigned variation=0;variation<4;++variation) {
        prepare();original[0x1c9d0]=std::uint8_t(context);original[0x57c3c]=std::uint8_t(network);
        original[0x1ca2c]=original[0x1ca2d]=std::uint8_t(fuel);
        run(0x5d726,scr::runtime::initializeNativePlayerConfiguration,"player-configuration");
    }
    for(unsigned variation=0;variation<32;++variation) {
        prepare();original[0x57c3c]=0;original[0x1c9d0]=0;
        run(0x5a9b6,scr::runtime::initializeNativeGameData,"game-data");
        prepare();run(0x5aa38,scr::runtime::initializeNativeGameTables,"game-tables");
    }
    std::cout<<"{\"cases\":"<<cases<<",\"compared_bytes\":"<<compared<<",\"mismatches\":0,\"maximum_instructions\":"<<maximumInstructions
        <<",\"whole_ram_compared\":true,\"loader_os_vector_bytes_excluded\":4,\"original_code_unmodified\":true,\"native_state_reseeded_with_original_outputs\":false,\"captured_state_used\":false,\"hardware_side_effects_compared\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

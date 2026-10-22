#include "native_startup.h"
#include "native_damage_holes.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Memory=scr::viewer::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},frame{};
std::uint64_t totalInstructions{},cases{},comparedBytes{};
bool invalid{},returned{};
std::string failure;
unsigned word(const Memory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const Memory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void word(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(Memory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
void hook(unsigned a){
    pc=a;if(invalid){m68k_end_timeslice();return;}
    if(a==sentinel){returned=true;m68k_end_timeslice();return;}
    if((a&1)||!(in(a,0x6091a,0x609ae)||in(a,0x69f42,0x69fe8))||++instructions>200000){fail("Unexpected PC/budget",a);m68k_end_timeslice();}
}
std::uint8_t read(unsigned a){
    if(a<ram.size())return ram[a];if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;fail("Unexpected read",a);return 0;
}
void write(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    bool coordinate=false;for(unsigned id=23;id<=28;++id)coordinate|=in(a,0x6a174+id*16,0x6a176+id*16);
    if(a<ram.size()&&(in(a,frame,frame+64000)||in(a,0x6a58c,0x6a590)||coordinate)){ram[a]=v;return;}
    fail("Unexpected write",a);
}
void run(){
    invalid=returned=false;failure.clear();instructions=0;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,0x60988);m68k_execute(10000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Original damage holes failed: "+failure);
    totalInstructions+=instructions;
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return read(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)fail("Odd word read",a);return (unsigned(read(a))<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)fail("Odd word write",a);write(a,std::uint8_t(v>>8));write(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_damage_holes_tests asset-pack");
    auto startup=scr::viewer::startNativeRace(argv[1]);
    const auto& base=*startup.memory;frame=longword(base,0x6a584);
    for(auto range:std::array<std::array<unsigned,2>,2>{{{0x6091a,0x609ae},{0x69f42,0x69fe8}}})
        if(!std::equal(startup.payload.begin()+range[0]-0xe700,startup.payload.begin()+range[1]-0xe700,base.begin()+range[0]))
            throw std::runtime_error("Original damage-hole code changed during native initialization");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned position=0;position<=10;++position)for(unsigned variation=0;variation<8;++variation){
        ram=base;ram[0x1c9cf]=std::uint8_t(position);
        if(variation)for(unsigned a=frame;a<frame+64000;++a)ram[a]=std::uint8_t(a*37+(a>>7)*13+variation*71);
        longword(ram,0x6a58c,frame+((variation&1)?32000:0x284));
        const auto before=std::make_unique<Memory>(ram);auto native=std::make_unique<Memory>(ram);
        for(unsigned repeat=0;repeat<2;++repeat){
            scr::viewer::initializeNativeDamageHoles(*native);run();
            for(unsigned a=0;a<ram.size();++a){++comparedBytes;if(ram[a]!=(*native)[a])
                throw std::runtime_error("Damage-hole mismatch position="+std::to_string(position)+" variation="+std::to_string(variation)+" address="+std::to_string(a));}
            if(longword(*native,0x625a2)!=longword(*before,0x625a2)||(*native)[0x625a6]!=(*before)[0x625a6]||
                    longword(*native,0x6a58c)!=longword(*before,0x6a58c)||(*native)[0x1c9cf]!=position)
                throw std::runtime_error("Damage-hole initialization changed gameplay RNG, position or buffer selection");
            ++cases;
        }
    }
    unsigned rejected=0;
    for(unsigned kind=0;kind<4;++kind){auto native=std::make_unique<Memory>(base);
        if(kind==0)(*native)[0x1c9cf]=11;if(kind==1)longword(*native,0x6a584,0x7ffff);
        if(kind==2)word(*native,0x6a16c+23*16+4,2);if(kind==3)longword(*native,0x6a4ac+27*4,0);
        try{scr::viewer::initializeNativeDamageHoles(*native);}catch(const std::invalid_argument&){++rejected;}
    }
    if(rejected!=4)throw std::runtime_error("Damage-hole initializer admitted an invalid domain");
    std::cout<<"{\"cases\":"<<cases<<",\"compared_bytes\":"<<comparedBytes<<",\"original_instructions\":"<<totalInstructions
        <<",\"mismatches\":0,\"hole_positions\":11,\"invalid_domains_rejected\":"<<rejected
        <<",\"whole_ram_compared\":true,\"both_buffers_compared\":true,\"original_code_unmodified\":true,\"rng_preserved\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

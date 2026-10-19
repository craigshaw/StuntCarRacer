#include "track_initialization.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
scr::NativeMemory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},totalInstructions{};bool invalid{},returned{};std::string failure;
std::uint16_t word(const scr::NativeMemory& r,unsigned a){return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
std::uint32_t longword(const scr::NativeMemory& r,unsigned a){return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* what,unsigned a){invalid=true;if(failure.empty())failure=std::string(what)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
bool code(unsigned a){return in(a,0x5ae00,0x5b2c4)||in(a,0x5b2c8,0x5b32e)||in(a,0x5fe56,0x5ff94)||in(a,0x63a8e,0x63b08)||in(a,0x62574,0x625a2)||in(a,0x5d85a,0x5d87e)||in(a,0x64304,0x64338)||in(a,0x65bec,0x65e30)||in(a,0x65756,0x65a18)||in(a,0x5c51a,0x5c554)||in(a,0x5c6c4,0x5c808);}
void hook(unsigned a){pc=a;if(a==sentinel){returned=true;m68k_end_timeslice();return;}if(invalid||!code(a)||++instructions>2000000){fail("Code/budget",a);m68k_end_timeslice();}}
std::uint8_t readByte(unsigned a){if(a<ram.size())return ram[a];if(in(a,stackBase,stackTop))return stack[a-stackBase];if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;fail("Read",a);return 0;}
void writeByte(unsigned a,std::uint8_t value){if(in(a,stackBase,stackTop)){stack[a-stackBase]=value;return;}if(in(a,0x1baf8,0x1ca36)||in(a,0x625a2,0x625a7)||in(a,0x63ce0,0x63ce3)||in(a,0x65e70,0x65ed4)||in(a,0x66100,0x66106)||in(a,0x7aa1a,0x80000)){ram[a]=value;return;}fail("Write",a);}
void run(unsigned entry,unsigned track){
    m68k_pulse_reset();for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_D1,track);
    for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(sentinel>>(24-8*n));
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);invalid=returned=false;failure.clear();instructions=0;
    m68k_execute(100000000);totalInstructions+=instructions;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Track original execution failed "+failure);
}
std::vector<std::uint8_t> readFile(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read payload");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
unsigned comparisons{};
void compare(const scr::NativeMemory& native,unsigned first,unsigned end,unsigned track){for(unsigned a=first;a<end;++a){++comparisons;if(native[a]!=ram[a])throw std::runtime_error("Track mismatch track="+std::to_string(track)+" address="+std::to_string(a)+" original="+std::to_string(ram[a])+" native="+std::to_string(native[a]));}}
}
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned m68k_read_memory_16(unsigned a){if(a&1)fail("Odd word read",a);return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned value){writeByte(a,std::uint8_t(value));}
void m68k_write_memory_16(unsigned a,unsigned value){if(a&1)fail("Odd word write",a);writeByte(a,std::uint8_t(value>>8));writeByte(a+1,std::uint8_t(value));}
void m68k_write_memory_32(unsigned a,unsigned value){m68k_write_memory_16(a,value>>16);m68k_write_memory_16(a+2,value);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("Usage: track_initialization_oracle payload.bin");const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload length mismatch");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned cases=0,totalCacheBytes=0,emptySegmentRanges=0,endAliases=0;
    for(unsigned variant=0;variant<3;++variant)for(unsigned track=0;track<8;++track){
        ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);std::fill(ram.begin()+0x1baf8,ram.begin()+0x1ca36,0);
        // Deterministic starting seed, with two alternative player/difficulty
        // contexts. Generated buffers are poisoned, never copied from a capture.
        ram[0x1ca33]=std::uint8_t(track);ram[0x625a2]=std::uint8_t(0x24+variant*11);ram[0x625a6]=std::uint8_t(0x51+variant*17);
        if(variant==1)ram[0x1c9d0]=1;
        if(variant==2){ram[0x5eb76]=1;ram[0x1ca35]=1;ram[0x63ce1]=0x46;}
        std::fill(ram.begin()+0x7aa1a,ram.end(),0xa5);std::fill_n(ram.begin()+0x65e70,100,0x5a);
        auto native=ram;scr::initializeNativeTrack(native,track);
        run(0x5d85a,track);run(0x5ae46,track);run(0x64304,track);run(0x65bec,track);
        compare(native,0x1bdd0,0x1bf6c,track);compare(native,0x1c280,0x1c380,track);compare(native,0x1c4c0,0x1ca36,track);compare(native,0x625a2,0x625a7,track);compare(native,0x63ce0,0x63ce3,track);
        compare(native,0x65e70,0x65ed4,track);compare(native,0x7aa1a,0x80000,track);compare(native,0x66102,0x66106,track);
        const auto end=longword(ram,0x66102);const auto cacheBytes=end-0x7abda;totalCacheBytes+=cacheBytes;++cases;
        // A pointer is written before any sections are admitted. Suppressed
        // segments legitimately share the next segment's pointer; a trailing
        // empty range can use the stream's end boundary.
        unsigned empty=0;
        for(unsigned segment=0;segment<ram[0x1ca1a];++segment){
            const auto first=longword(ram,0x7aa1a+4*segment);
            const auto last=segment+1<ram[0x1ca1a]?longword(ram,0x7aa1a+4*(segment+1)):end;
            if(first==last){++empty;++emptySegmentRanges;if(first==end)++endAliases;}
        }
        std::cout<<"track "<<track<<" variant "<<variant<<" segments "<<unsigned(ram[0x1ca1a])<<" cache "<<cacheBytes<<" empty_segment_ranges "<<empty<<"\n";
    }
    std::cout<<"{\"cases\":"<<cases<<",\"comparisons\":"<<comparisons<<",\"instructions\":"<<totalInstructions<<",\"total_cache_bytes\":"<<totalCacheBytes<<",\"empty_segment_ranges\":"<<emptySegmentRanges<<",\"stream_end_aliases\":"<<endAliases<<",\"mismatches\":0}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

// Test-only execution of original payload bytes, with the original trig table.
#include "physics/expected_wheel_heights.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned base=0xe700, entry=0x61b70, end=0x61bcc;
constexpr unsigned trigBegin=0x64d08, trigEnd=0x64d66, tableBegin=0x1ca42, tableEnd=0x1cc46;
constexpr unsigned stack=0x7f000, sentinel=0x7e000;
constexpr unsigned pitchAddress=0x1bce4, rollAddress=0x1bce8, heightAddress=0x1bcdc;
constexpr unsigned outputBegin=0x1bc94, scratch=0x1bbf6;
constexpr std::uint32_t seed=0x48475431;
std::uint32_t rng=seed;
std::array<std::uint8_t,0x80000> ram{};
scr::TrigTable table{};
bool executing=false, invalid=false, returned=false;
unsigned currentPc=0, instructions=0, maxInstructions=0, dataWrites=0, stackWrites=0, calls=0;
unsigned pendingReturn=0, stackMask=0;
std::array<bool,14> outputWritten{};
std::uint64_t total=0;
using Results=std::array<std::uint32_t,4>;

std::uint32_t random32() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
std::uint16_t word(unsigned a) { return std::uint16_t(unsigned(ram[a])*256+ram[a+1]); }
std::uint32_t longword(unsigned a) { return (std::uint32_t(word(a))<<16)|word(a+2); }
void putWord(unsigned a,std::uint16_t v) { ram[a]=std::uint8_t(v>>8); ram[a+1]=std::uint8_t(v); }
void putLong(unsigned a,std::uint32_t v) { putWord(a,std::uint16_t(v>>16)); putWord(a+2,std::uint16_t(v)); }
bool inside(unsigned a,unsigned n,unsigned begin,unsigned finish) {
    return a>=begin && a<finish && n<=finish-a;
}
bool readable(unsigned a,unsigned n) {
    if(!inside(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))) return false;
    if(!executing) return true;
    return inside(a,n,entry,end+4) || inside(a,n,trigBegin,trigEnd+4)
        || inside(a,n,tableBegin,tableEnd) || inside(a,n,stack-4,stack+4)
        || inside(a,n,sentinel,sentinel+4) || inside(a,n,scratch,scratch+2)
        || inside(a,n,pitchAddress,pitchAddress+2) || inside(a,n,rollAddress,rollAddress+2)
        || inside(a,n,heightAddress,heightAddress+4);
}
bool writable(unsigned a,unsigned n,unsigned value) {
    if(!executing || (a&1)) { invalid=true; return false; }
    unsigned first=0;
    if(inside(a,n,outputBegin,outputBegin+12)) first=a-outputBegin;
    else if(inside(a,n,scratch,scratch+2)) first=12+a-scratch;
    else if(pendingReturn && inside(a,n,stack-4,stack)) {
        for(unsigned i=0;i<n;++i) {
            const auto index=a+i-(stack-4);
            if((stackMask&(1u<<index))
               || ((value>>(8*(n-1-i)))&255)!=((pendingReturn>>(8*(3-index)))&255)) {
                invalid=true; return false;
            }
            stackMask|=1u<<index;
        }
        stackWrites+=n; return true;
    } else { invalid=true; return false; }
    for(unsigned i=0;i<n;++i) {
        if(outputWritten[first+i]) { invalid=true; return false; }
        outputWritten[first+i]=true;
    }
    dataWrites+=n; return true;
}
void hook(unsigned pc) {
    if(pendingReturn && stackMask!=15) invalid=true;
    pendingReturn=stackMask=0; currentPc=pc;
    if(invalid) { m68k_end_timeslice(); return; }
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    if((pc&1) || (!(pc>=entry && pc<end) && !(pc>=trigBegin && pc<trigEnd))
       || ++instructions>100) { invalid=true; m68k_end_timeslice(); return; }
    if(pc==0x61b76 || pc==0x61b88) {
        if(word(pc)!=0x4eb9 || longword(pc+2)!=trigBegin || m68k_get_reg(nullptr,M68K_REG_SP)!=stack) {
            invalid=true; m68k_end_timeslice(); return;
        }
        pendingReturn=pc+6; ++calls;
    }
}
Results readResults() { return {longword(outputBegin),longword(outputBegin+4),longword(outputBegin+8),word(scratch)}; }
Results check(std::uint16_t pitch,std::uint16_t roll,std::uint32_t height) {
    putWord(pitchAddress,pitch); putWord(rollAddress,roll); putLong(heightAddress,height);
    putWord(scratch,std::uint16_t(random32()));
    for(unsigned a=outputBegin;a<outputBegin+12;a+=4) putLong(a,random32());
    putLong(0,stack); putLong(4,entry); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700);
    executing=false; m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),random32());
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false; instructions=dataWrites=stackWrites=calls=pendingReturn=stackMask=0;
    outputWritten.fill(false); executing=true;
    m68k_execute(5000); executing=false;
    if(invalid || !returned || dataWrites!=14 || stackWrites!=8 || calls!=2
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4
       || !std::all_of(outputWritten.begin(),outputWritten.end(),[](bool value){return value;}))
        throw std::runtime_error("Original height execution/access/stack contract failed at PC "+std::to_string(currentPc));
    const auto expected=readResults();
    const auto result=scr::calculateExpectedWheelHeights(pitch,roll,height,table);
    const Results actual{std::uint32_t(result.frontLeft),std::uint32_t(result.frontRight),
                         std::uint32_t(result.rearCenter),result.pitchSine};
    if(expected!=actual) {
        std::cerr<<"Height mismatch case="<<total<<" pitch="<<pitch<<" roll="<<roll<<" height="<<height;
        throw std::runtime_error("Original/native height mismatch");
    }
    maxInstructions=std::max(maxInstructions,instructions); ++total; return actual;
}
void printResults(const Results& values) {
    std::cout<<'[';
    for(std::size_t i=0;i<values.size();++i) { if(i) std::cout<<','; std::cout<<values[i]; }
    std::cout<<']';
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!readable(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!readable(a,2)) { invalid=true; return 0; } return word(a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    if(!readable(a,4)) { invalid=true; return 0; } return longword(a);
}
void m68k_write_memory_8(unsigned int,unsigned int) { invalid=true; }
void m68k_write_memory_16(unsigned int a,unsigned int v) { if(writable(a,2,v)) putWord(a,std::uint16_t(v)); }
void m68k_write_memory_32(unsigned int a,unsigned int v) { if(writable(a,4,v)) putLong(a,v); }
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: expected_wheel_heights_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=408540) throw std::runtime_error("Expected original 408540-byte payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);
    for(std::size_t i=0;i<table.size();++i) table[i]=word(tableBegin+unsigned(i*2));
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<seed<<",\"captures\":[";
    for(int i=2;i<argc;i+=2) {
        const std::string label=argv[i];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[i+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Capture size mismatch");
        for(auto range:std::array<std::array<unsigned,2>,3>{{{entry,end},{trigBegin,trigEnd},{tableBegin,tableEnd}}})
            if(!std::equal(payload.begin()+range[0]-base,payload.begin()+range[1]-base,capture.begin()+range[0]))
                throw std::runtime_error("Capture code/table identity mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto stored=readResults();
        const auto pitch=word(pitchAddress),roll=word(rollAddress);
        const auto height=longword(heightAddress);
        const auto replayed=check(pitch,roll,height);
        if(i>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"pitch\":"<<pitch<<",\"roll\":"<<roll<<",\"world_height_bits\":"<<height
                 <<",\"replayed_original_and_cpp\":"; printResults(replayed);
        std::cout<<",\"stored_snapshot\":"; printResults(stored);
        std::cout<<",\"stored_snapshot_agrees\":"<<(stored==replayed?"true":"false")<<'}';
    }
    const auto captured=total;
    ram.fill(0); std::copy(payload.begin(),payload.end(),ram.begin()+base);
    for(unsigned value=0;value<65536;++value) {
        check(std::uint16_t(value),0xbfe1,0x7fffff81);
        check(0x41,std::uint16_t(value),0x8000007f);
    }
    constexpr std::array<std::uint32_t,4> highWords{0,0x7fff0000,0x80000000,0xffff0000};
    for(auto high:highWords) for(unsigned value=0;value<65536;++value)
        check(0x3fff,0xc041,high|value);
    const auto swept=total-captured;
    constexpr std::array<std::uint16_t,15> angles{0,1,63,64,65,0x3fff,0x4000,0x4001,0x7fff,0x8000,0x8001,0xbfff,0xc000,0xc001,0xffff};
    constexpr std::array<std::uint32_t,18> heights{0,1,255,256,257,0x7fffffff,0x80000000,0xffffffff,
        0x7ff80000,0x7ff80001,0x80080000,0x8007ffff,0x7ffc0000,0x80040000,0xfffffeff,0xffffff00,0xffffff01,0xfffffffe};
    const auto beforeBoundary=total;
    for(auto pitch:angles) for(auto roll:angles) for(auto height:heights) check(pitch,roll,height);
    const auto boundary=total-beforeBoundary;
    for(unsigned n=0;n<100000;++n) {
        const auto pitch=std::uint16_t(random32()),roll=std::uint16_t(random32());
        const auto height=random32(); check(pitch,roll,height);
    }
    std::cout<<"],\"capture_cases\":"<<captured<<",\"independent_sweep_cases\":"<<swept
             <<",\"boundary_cases\":"<<boundary<<",\"random_cases\":100000,\"total_cases\":"<<total
             <<",\"mismatches\":0,\"max_instructions_per_case\":"<<maxInstructions
             <<",\"output_bytes_checked\":14,\"stack_bytes_per_case\":8,\"nested_calls_per_case\":2"
             <<",\"restricted_reads_writes_pc_and_return_verified\":true,\"final_prng_state\":"<<rng<<"}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<'\n'<<error.what()<<'\n'; return 1; }

// Test-only 68000 execution of the actual recovered bytes. The native library
// does not link this core. Supply a verified 130-byte code file, never a source
// re-encoding, so the oracle is independent of the C++ arithmetic formulation.
#include "wheel_coordinates.h"
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
constexpr unsigned entry = 0x618ce, end = 0x61950;
constexpr unsigned stack = 0x7f000, sentinel = 0x7e000;
constexpr std::array<unsigned, 6> inputs{0x1c264,0x1c268,0x1c26e,0x1c272,0x1c274,0x1c276};
constexpr std::array<unsigned, 6> outputs{0x1bd02,0x1bd04,0x1bd06,0x1bd08,0x1bd0a,0x1bd0c};
using Words = std::array<std::uint16_t,6>;
std::array<std::uint8_t, 0x80000> ram{};
unsigned instructionCount = 0, writeCount = 0;
bool invalidAccess = false, returned = false, executing = false;
std::uint64_t total = 0;
std::uint32_t randomState = 0x53435231;

std::uint32_t random32() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("Cannot read ") + path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
std::uint16_t word(unsigned a) { return std::uint16_t((unsigned(ram[a]) << 8) | ram[a+1]); }
void putWord(unsigned a, std::uint16_t v) { ram[a]=std::uint8_t(v>>8); ram[a+1]=std::uint8_t(v); }
void putLong(unsigned a, std::uint32_t v) { putWord(a,std::uint16_t(v>>16)); putWord(a+2,std::uint16_t(v)); }
Words getWords(const std::array<unsigned,6>& addresses) {
    Words values{};
    for (std::size_t i=0;i<values.size();++i) values[i]=word(addresses[i]);
    return values;
}
Words nativeWords(const Words& in) {
    const auto p=scr::calculateWheelCoordinates({in[0],in[1],in[2],in[3],in[4],in[5]});
    // Signed-to-unsigned conversion is defined modulo 2^16.
    return {std::uint16_t(p.frontLeft.x),std::uint16_t(p.frontRight.x),std::uint16_t(p.rearCenter.x),
            std::uint16_t(p.frontLeft.y),std::uint16_t(p.frontRight.y),std::uint16_t(p.rearCenter.y)};
}
void instructionHook(unsigned pc) {
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); }
    else if(pc<entry || pc>=end || ++instructionCount>29) { invalidAccess=true; m68k_end_timeslice(); }
}
Words originalWords(const Words& in) {
    for(std::size_t i=0;i<6;++i) { putWord(inputs[i],in[i]); putWord(outputs[i],0xa55a); }
    putLong(0,stack); putLong(4,entry); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700); // STOP after RTS, outside routine
    executing=false;
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),random32());
    m68k_set_reg(M68K_REG_SR,0x2700 | (random32()&31));
    m68k_set_reg(M68K_REG_SP,stack);
    m68k_set_reg(M68K_REG_PC,entry);
    instructionCount=writeCount=0; invalidAccess=returned=false; executing=true;
    m68k_execute(2000);
    executing=false;
    if(invalidAccess || !returned || instructionCount!=29 || writeCount!=12 || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Oracle execution/access/return contract failed");
    return getWords(outputs);
}
Words check(const Words& in) {
    const auto expected=originalWords(in), actual=nativeWords(in);
    if(expected!=actual) {
        std::cerr<<"Mismatch at case "<<total<<" input:";
        for(auto w:in) std::cerr<<' '<<w;
        std::cerr<<"\nOriginal:"; for(auto w:expected) std::cerr<<' '<<w;
        std::cerr<<"\nC++:"; for(auto w:actual) std::cerr<<' '<<w;
        throw std::runtime_error("Differential mismatch");
    }
    ++total;
    return actual;
}
void printWords(const Words& w) {
    std::cout<<'[';
    for(std::size_t i=0;i<6;++i) { if(i) std::cout<<','; std::cout<<unsigned(w[i]); }
    std::cout<<']';
}
bool outputAddress(unsigned address,unsigned size) {
    return size==2 && std::find(outputs.begin(),outputs.end(),address)!=outputs.end();
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(a>=ram.size()) { invalidAccess=true; return 0; }
    return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if((a&1) || a+1>=ram.size()) { invalidAccess=true; return 0; }
    return word(a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return (m68k_read_memory_16(a)<<16) | m68k_read_memory_16(a+2);
}
void m68k_write_memory_8(unsigned int, unsigned int) { invalidAccess=true; }
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(!executing || !outputAddress(a,2)) { invalidAccess=true; return; }
    ++writeCount; putWord(a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int,unsigned int) { invalidAccess=true; }
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: wheel_oracle code.bin [label chip-ram.bin]...");
    const auto code=readFile(argv[1]);
    if(code.size()!=end-entry) throw std::runtime_error("Expected exactly 130 original code bytes");
    std::copy(code.begin(),code.end(),ram.begin()+entry);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_instr_hook_callback(instructionHook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":1396920881,\"captures\":[";
    for(int a=2;a<argc;a+=2) {
        const std::string label=argv[a];
        if(label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[a+1]);
        if(capture.size()!=ram.size() || !std::equal(code.begin(),code.end(),capture.begin()+entry))
            throw std::runtime_error("Capture size or original code identity mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto in=getWords(inputs), stored=getWords(outputs), result=check(in);
        if(a>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"input_words\":"; printWords(in);
        std::cout<<",\"replayed_original_and_cpp_words\":"; printWords(result);
        std::cout<<",\"stored_snapshot_words\":"; printWords(stored);
        std::cout<<",\"stored_snapshot_agrees\":"<<(stored==result?"true":"false")<<'}';
    }
    const auto snapshotCases=total;
    // Sweep every bit pattern in each input independently. Other fields include
    // odd negatives/extremes to catch incorrect signed interpretation/rounding.
    const Words fixed{0x8001,0x7fff,0xffff,0x0041,0xffdf,0x8000};
    for(std::size_t field=0;field<6;++field) for(unsigned w=0;w<65536;++w) {
        auto in=fixed; in[field]=std::uint16_t(w); check(in);
    }
    const auto sweepCases=total-snapshotCases;
    constexpr std::array<int,21> edge{-32768,-32767,-65,-64,-63,-33,-32,-31,-2,-1,0,1,2,31,32,33,63,64,65,32766,32767};
    for(int a:edge) for(int b:edge) for(int c:edge)
        check({std::uint16_t(a),std::uint16_t(b),std::uint16_t(b),std::uint16_t(a),std::uint16_t(c),std::uint16_t(c)});
    for(unsigned n=0;n<250000;++n) {
        Words in{}; for(auto& w:in) w=std::uint16_t(random32()); check(in);
    }
    std::cout<<"],\"single_word_sweep_cases\":"<<sweepCases
             <<",\"boundary_cases\":9261,\"random_cases\":250000,\"total_cases\":"<<total
             <<",\"mismatches\":0,\"writes_confined_to_six_outputs\":true,\"instructions_per_case\":29}\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"\n"<<e.what()<<'\n'; return 1; }

// Differential test against the recovered 68000 routine, not a second C++
// formulation of interpolation. Musashi is linked only into this test target.
#include "track/track_interpolation.h"
#include "m68k.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned loadAddress=0xe700, payloadSize=408540;
constexpr unsigned entry=0x5c554, end=0x5c5f2, output=0x1bb18;
constexpr unsigned lateralAddress=0x1bc4d, blendAddress=0x1bc41;
constexpr std::array<unsigned,4> controlAddresses{0x1bc02,0x1bc04,0x1bc06,0x1bc08};
constexpr unsigned stack=0x7f000, sentinel=0x7e000;
constexpr std::uint32_t initialSeed=0x53435233, initialCpuSeed=0x36383031;
using Corners=std::array<std::uint16_t,4>;
struct Case { Corners corners; std::uint8_t lateral,blend; };
std::array<std::uint8_t,0x80000> ram{};
std::uint32_t randomState=initialSeed,cpuRandomState=initialCpuSeed;
std::uint64_t totalCases=0,totalInstructions=0;
std::array<std::uint64_t,4> branches{}; // fine positive/negative; coarse positive/negative
unsigned currentPc=0,instructions=0,outputMask=0,outputBytes=0;
unsigned minimumInstructions=1000,maximumInstructions=0,branchIndex=4;
bool executing=false,invalidAccess=false,returned=false;
std::string accessFailure;

std::uint32_t random32(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool contains(unsigned address,unsigned size,unsigned begin,unsigned limit) {
    return address>=begin && address<limit && size<=limit-address;
}
std::uint16_t word(unsigned address) {
    return std::uint16_t((unsigned(ram[address])<<8)|ram[address+1]);
}
std::uint32_t longword(unsigned address) { return (std::uint32_t(word(address))<<16)|word(address+2); }
void putWord(unsigned address,std::uint16_t value) {
    ram[address]=std::uint8_t(value>>8); ram[address+1]=std::uint8_t(value);
}
void putLong(unsigned address,std::uint32_t value) {
    putWord(address,std::uint16_t(value>>16)); putWord(address+2,std::uint16_t(value));
}
void failAccess(const char* description,unsigned address,unsigned size) {
    invalidAccess=true;
    if(accessFailure.empty()) {
        std::ostringstream message;
        message<<description<<" address=0x"<<std::hex<<address<<" size="<<std::dec<<size
               <<" instruction_pc=0x"<<std::hex<<currentPc;
        accessFailure=message.str();
    }
}
bool permittedRead(unsigned address,unsigned size) {
    if(!contains(address,size,0,unsigned(ram.size())) || (size>1 && (address&1))) return false;
    if(!executing) return true;
    return contains(address,size,entry,end+4)
        || contains(address,size,sentinel,sentinel+8)
        || contains(address,size,stack,stack+4)
        || contains(address,size,controlAddresses.front(),controlAddresses.back()+2)
        || (size==1 && (address==lateralAddress || address==blendAddress));
}
bool permittedWrite(unsigned address,unsigned size) {
    if(!executing || currentPc!=0x5c5ea || (address&1)
       || !contains(address,size,output,output+4) || (size!=2 && size!=4)) {
        failAccess("Write outside sole longword result",address,size); return false;
    }
    for(unsigned byte=0;byte<size;++byte) {
        const unsigned mask=1u<<(address+byte-output);
        if(outputMask&mask) { failAccess("Repeated output byte",address,size); return false; }
        outputMask|=mask;
    }
    outputBytes+=size; return true;
}
void instructionHook(unsigned pc) {
    currentPc=pc;
    if(invalidAccess) { m68k_end_timeslice(); return; }
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    if(pc<entry || pc>=end || (pc&1) || ++instructions>48) {
        failAccess("Instruction outside routine/budget",pc,2); m68k_end_timeslice(); return;
    }
    // Record actual interpreter branches, independently of the native formula.
    switch(pc) {
    case 0x5c5e4: branchIndex=0; break;
    case 0x5c5d6: branchIndex=1; break;
    case 0x5c5c8: branchIndex=2; break;
    case 0x5c5ba: branchIndex=3; break;
    default: break;
    }
}
std::uint32_t originalResult(const Case& input) {
    for(std::size_t i=0;i<input.corners.size();++i) putWord(controlAddresses[i],input.corners[i]);
    ram[lateralAddress]=input.lateral; ram[blendAddress]=input.blend;
    putLong(output,random32(randomState));
    putLong(0,stack); putLong(4,entry); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700);
    executing=false;
    m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRandomState));
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRandomState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    instructions=outputMask=outputBytes=0; branchIndex=4;
    invalidAccess=returned=false; accessFailure.clear(); currentPc=entry; executing=true;
    m68k_execute(3000);
    executing=false;
    if(invalidAccess || !returned || outputMask!=15 || outputBytes!=4
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4 || branchIndex>=branches.size()) {
        std::ostringstream error;
        error<<"Oracle access/return/output contract failed: "<<accessFailure
             <<" returned="<<returned<<" instructions="<<instructions<<" output_bytes="<<outputBytes;
        throw std::runtime_error(error.str());
    }
    const auto result=m68k_get_reg(nullptr,M68K_REG_D0);
    if(result!=longword(output)) throw std::runtime_error("Original D0 differs from original stored result");
    ++branches[branchIndex]; totalInstructions+=instructions;
    minimumInstructions=std::min(minimumInstructions,instructions);
    maximumInstructions=std::max(maximumInstructions,instructions);
    return result;
}
std::uint32_t check(const Case& input) {
    const auto expected=originalResult(input);
    const auto actual=scr::interpolateTrackCoordinate({input.corners[0],input.corners[1],input.corners[2],input.corners[3],
                                                      input.lateral,input.blend});
    if(expected!=actual) {
        std::cerr<<"Interpolation mismatch at case "<<totalCases<<" corners:";
        for(auto corner:input.corners) std::cerr<<' '<<corner;
        std::cerr<<" lateral="<<unsigned(input.lateral)<<" blend="<<unsigned(input.blend)
                 <<" original="<<expected<<" native="<<actual;
        throw std::runtime_error("Track interpolation differential mismatch");
    }
    ++totalCases; return actual;
}
void printCorners(const Corners& corners) {
    std::cout<<'[';
    for(std::size_t i=0;i<corners.size();++i) { if(i) std::cout<<','; std::cout<<corners[i]; }
    std::cout<<']';
}
Case captureInputs() {
    Case input{};
    for(std::size_t i=0;i<input.corners.size();++i) input.corners[i]=word(controlAddresses[i]);
    input.lateral=ram[lateralAddress]; input.blend=ram[blendAddress]; return input;
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int address) {
    if(!permittedRead(address,1)) { failAccess("Invalid read",address,1); return 0; }
    return ram[address];
}
unsigned int m68k_read_memory_16(unsigned int address) {
    if(!permittedRead(address,2)) { failAccess("Invalid read",address,2); return 0; }
    return word(address);
}
unsigned int m68k_read_memory_32(unsigned int address) {
    if(!permittedRead(address,4)) { failAccess("Invalid read",address,4); return 0; }
    return longword(address);
}
void m68k_write_memory_8(unsigned int address,unsigned int) { failAccess("Unexpected byte write",address,1); }
void m68k_write_memory_16(unsigned int address,unsigned int value) {
    if(permittedWrite(address,2)) putWord(address,std::uint16_t(value));
}
void m68k_write_memory_32(unsigned int address,unsigned int value) {
    if(permittedWrite(address,4)) putLong(address,value);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2)
        throw std::runtime_error("Usage: track_interpolation_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Expected original 408540-byte payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(instructionHook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<initialSeed
             <<",\"cpu_register_seed\":"<<initialCpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()
           || !std::equal(payload.begin()+entry-loadAddress,payload.begin()+end-loadAddress,capture.begin()+entry))
            throw std::runtime_error("Capture size or original code identity mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto input=captureInputs(); const auto stored=longword(output); const auto result=check(input);
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"input_control_words\":"; printCorners(input.corners);
        std::cout<<",\"lateral\":"<<unsigned(input.lateral)<<",\"blend\":"<<unsigned(input.blend)
                 <<",\"replayed_original_and_cpp_result\":"<<result<<",\"stored_snapshot_result\":"<<stored
                 <<",\"stored_snapshot_agrees\":"<<(stored==result?"true":"false")<<'}';
    }
    const auto captureCases=totalCases;
    ram.fill(0); std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);
    // Every fraction pair for twelve representative flat, sloped, opposite-sign
    // and word-wrapped corner configurations; not an exhaustive corner domain.
    constexpr std::array<Corners,12> representative{{
        {0,0,0,0},{0x7fff,0x7fff,0x7fff,0x7fff},{0x8000,0x8000,0x8000,0x8000},
        {0,256,512,768},{0,0,1,1},{0,0,0xffff,0xffff},
        {0,0,128,128},{0,0,0xff80,0xff80},{0x7fff,0x8000,0x8000,0x7fff},
        {0xffff,1,1,0xffff},{0x8000,0,0,0x8000},{0x1234,0xfedc,0x89ab,0x7654}}};
    for(const auto& corners:representative) for(unsigned lateral=0;lateral<256;++lateral)
        for(unsigned blend=0;blend<256;++blend) check({corners,std::uint8_t(lateral),std::uint8_t(blend)});
    const auto fractionCases=totalCases-captureCases;
    const auto beforeSweep=totalCases;
    constexpr Corners fixed{0x8001,0x7ffe,0xf01f,0x1234};
    for(std::size_t field=0;field<4;++field) for(unsigned value=0;value<65536;++value) {
        auto corners=fixed; corners[field]=std::uint16_t(value); check({corners,0xb7,0xd3});
    }
    const auto sweepCases=totalCases-beforeSweep;
    const auto beforeDelta=totalCases;
    // Construct exact inter-edge deltas q*256+r with a zero first edge and a
    // one-unit slope on the second. Probe both signs around the ASR3 threshold,
    // subsequent signed-word wrap boundaries, low-byte rounding and extremes.
    std::set<int> deltas;
    constexpr std::array<int,7> bases{0,32768,262144,524288,786432,1048576,0x7fff00};
    constexpr std::array<int,15> offsets{-257,-256,-255,-9,-8,-7,-1,0,1,7,8,9,255,256,257};
    for(int base:bases) for(int offset:offsets) for(int sign:{-1,1}) {
        const int delta=sign*(base+offset);
        if(delta>=-8388608 && delta<=8388607) deltas.insert(delta);
    }
    deltas.insert(-8388608); deltas.insert(8388607);
    for(int delta:deltas) {
        const int quotient=delta/256-(delta%256<0 ? 1 : 0);
        const int remainder=delta-quotient*256;
        const Corners corners{0,0,std::uint16_t(quotient),std::uint16_t(quotient+1)};
        for(unsigned blend=0;blend<256;++blend) check({corners,std::uint8_t(remainder),std::uint8_t(blend)});
    }
    const auto deltaCases=totalCases-beforeDelta;
    const auto beforeCorner=totalCases;
    constexpr std::array<std::uint16_t,9> edges{0,1,0xffff,0x7fff,0x8000,0x8001,0x00ff,0xff00,0x0100};
    constexpr std::array<std::array<std::uint8_t,2>,7> fractions{{{0,0},{255,255},{1,255},{255,1},{127,128},{128,127},{129,129}}};
    for(auto a:edges) for(auto b:edges) for(auto c:edges) for(auto d:edges)
        for(const auto& fraction:fractions) check({{a,b,c,d},fraction[0],fraction[1]});
    const auto cornerCases=totalCases-beforeCorner;
    constexpr unsigned randomCases=250000;
    for(unsigned i=0;i<randomCases;++i) {
        Case input{};
        for(auto& corner:input.corners) corner=std::uint16_t(random32(randomState));
        input.lateral=std::uint8_t(random32(randomState)); input.blend=std::uint8_t(random32(randomState)); check(input);
    }
    if(std::find(branches.begin(),branches.end(),std::uint64_t(0))!=branches.end())
        throw std::runtime_error("One original interpolation branch was never covered");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"representative_corner_sets\":"<<representative.size()
             <<",\"fraction_pair_cases\":"<<fractionCases<<",\"control_word_sweep_cases\":"<<sweepCases
             <<",\"targeted_deltas\":"<<deltas.size()<<",\"targeted_delta_cases\":"<<deltaCases
             <<",\"corner_boundary_cases\":"<<cornerCases<<",\"random_cases\":"<<randomCases
             <<",\"total_cases\":"<<totalCases<<",\"mismatches\":0"
             <<",\"original_return_and_memory_agree\":true,\"writes_confined_to_one_longword\":true"
             <<",\"reads_confined_to_inputs_code_and_return_stack\":true,\"stack_unchanged_except_rts_pop\":true"
             <<",\"minimum_instructions\":"<<minimumInstructions<<",\"maximum_instructions\":"<<maximumInstructions
             <<",\"total_instructions\":"<<totalInstructions<<",\"branch_cases\":{\"fine_positive\":"<<branches[0]
             <<",\"fine_negative\":"<<branches[1]<<",\"coarse_positive\":"<<branches[2]
             <<",\"coarse_negative\":"<<branches[3]<<"},\"final_input_prng_state\":"<<randomState
             <<",\"final_cpu_prng_state\":"<<cpuRandomState<<"}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<"\n"<<error.what()<<'\n'; return 1; }

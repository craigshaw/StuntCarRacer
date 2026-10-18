// Differential execution of original payload bytes, never reassembled source.
// Musashi is test-only; native functions have no emulated-register interface.
#include "wheel_motion_fixture.h"
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
namespace fixture=scr::test::wheelMotion;
constexpr unsigned load=0xe700, payloadSize=408540, stack=0x7f000, sentinel=0x7e000;
std::array<std::uint8_t,0x80000> ram{}, nativeRam{};
bool executing=false, invalid=false, returned=false, speedRoutine=false;
unsigned currentPc=0, instructions=0, maximumInstructions=0, outputMask=0, returns=0;
std::uint32_t randomState=0x574d4f31, registerState=0x36384d57;
std::uint64_t cases=0, speedCases=0, dampingCases=0;

std::uint32_t next(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
std::uint16_t randomWord() { return static_cast<std::uint16_t>(next(randomState)); }
bool contains(unsigned address,unsigned size,unsigned begin,unsigned end) {
    return address>=begin && address<end && size<=end-address;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool allowedRead(unsigned address,unsigned size) {
    if(!contains(address,size,0,unsigned(ram.size())) || (size>1 && (address&1))) return false;
    if(!executing) return true;
    if(contains(address,size,stack,stack+4) || contains(address,size,sentinel,sentinel+8)) return true;
    const auto range=fixture::codeRanges[speedRoutine?0:1];
    if(contains(address,size,range.begin,range.end+4)) return true;
    for(auto input:fixture::inputRanges) if(contains(address,size,input.begin,input.end)) return true;
    for(auto observed:fixture::observedRanges) if(contains(address,size,observed.begin,observed.end)) return true;
    return false;
}
void hook(unsigned pc) {
    currentPc=pc;
    if(pc==sentinel) { returned=true; ++returns; m68k_end_timeslice(); return; }
    const auto range=fixture::codeRanges[speedRoutine?0:1];
    if(!contains(pc,2,range.begin,range.end) || ++instructions>21) {
        invalid=true; m68k_set_reg(M68K_REG_PC,sentinel); m68k_end_timeslice();
    }
}
void original(bool speed) {
    speedRoutine=speed; executing=false;
    const auto entry=speed?fixture::speedEntry:fixture::dampingEntry;
    fixture::writeLong(ram.data(),0,stack); fixture::writeLong(ram.data(),4,entry);
    fixture::writeLong(ram.data(),stack,sentinel);
    fixture::writeWord(ram.data(),sentinel,0x4e72); fixture::writeWord(ram.data(),sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),next(registerState));
    m68k_set_reg(M68K_REG_SR,0x2700|(next(registerState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    instructions=outputMask=returns=0; invalid=returned=false; executing=true;
    m68k_execute(2000); executing=false;
    if(invalid || !returned || returns!=1 || outputMask!=3 || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Original execution/access/return contract failed, PC="+std::to_string(currentPc));
    maximumInstructions=std::max(maximumInstructions,instructions);
}
void preserveNativeState() {
    for(auto range:fixture::observedRanges)
        std::copy(ram.begin()+range.begin,ram.begin()+range.end,nativeRam.begin()+range.begin);
}
void compare() {
    for(auto range:fixture::observedRanges) for(auto a=range.begin;a<range.end;++a)
        if(ram[a]!=nativeRam[a]) {
            std::cerr<<"Mismatch case "<<cases<<" routine "<<(speedRoutine?"speed":"damping")
                     <<" address0x"<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])
                     <<" native="<<unsigned(nativeRam[a])<<'\n';
            throw std::runtime_error("Wheel motion differential mismatch");
        }
    ++cases;
}
scr::WheelSpeedState checkSpeed(const scr::WheelSpeedInputs& input,scr::WheelSpeedState state) {
    fixture::writeSpeedInputs(ram.data(),input); fixture::writeSpeedState(ram.data(),state);
    preserveNativeState();
    scr::updateWheelSpeed(input,state); fixture::writeSpeedState(nativeRam.data(),state);
    original(true); compare(); ++speedCases; return state;
}
scr::VelocityDampingState checkDamping(const scr::VelocityDampingInputs& input,scr::VelocityDampingState state) {
    fixture::writeDampingInputs(ram.data(),input); fixture::writeDampingState(ram.data(),state);
    preserveNativeState();
    scr::updateVelocityDamping(input,state); fixture::writeDampingState(nativeRam.data(),state);
    original(false); compare(); ++dampingCases; return state;
}
void randomizeGuards() {
    for(auto range:fixture::observedRanges) for(auto a=range.begin;a<range.end;++a)
        ram[a]=static_cast<std::uint8_t>(next(randomState));
}
std::vector<std::uint16_t> outputs() {
    return {fixture::readWord(ram.data(),0x1bd5c),fixture::readWord(ram.data(),0x1bc62),
            fixture::readWord(ram.data(),0x1bcfc),fixture::readWord(ram.data(),0x1bd00)};
}
void printWords(const std::vector<std::uint16_t>& values) {
    std::cout<<'['; for(std::size_t i=0;i<values.size();++i) { if(i) std::cout<<','; std::cout<<values[i]; } std::cout<<']';
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!allowedRead(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!allowedRead(a,2)) { invalid=true; return 0; } return fixture::readWord(ram.data(),a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);
}
void m68k_write_memory_8(unsigned int,unsigned int) { invalid=true; }
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    const auto& output=speedRoutine?fixture::speedOutputs:fixture::dampingOutputs;
    const auto found=std::find(output.begin(),output.end(),a);
    if(!executing || found==output.end()) { invalid=true; return; }
    const unsigned bit=1u<<unsigned(found-output.begin());
    if(outputMask&bit) { invalid=true; return; }
    outputMask|=bit; fixture::writeWord(ram.data(),a,static_cast<std::uint16_t>(v));
}
void m68k_write_memory_32(unsigned int,unsigned int) { invalid=true; }
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: wheel_motion_oracle payload.bin [label chip-ram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Unexpected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+load);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<randomState<<",\"captures\":[";
    for(int a=2;a<argc;a+=2) {
        const std::string label=argv[a];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[a+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Expected 512KiB capture");
        for(auto range:fixture::codeRanges)
            if(!std::equal(capture.begin()+range.begin,capture.begin()+range.end,payload.begin()+range.begin-load))
                throw std::runtime_error("Capture code differs from original");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto stored=outputs();
        checkSpeed(fixture::readSpeedInputs(ram.data()),fixture::readSpeedState(ram.data()));
        checkDamping(fixture::readDampingInputs(ram.data()),fixture::readDampingState(ram.data()));
        const auto replayed=outputs();
        if(a>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"stored_snapshot_words\":"; printWords(stored);
        std::cout<<",\"replayed_original_and_cpp_words\":"; printWords(replayed);
        std::cout<<",\"stored_snapshot_agrees\":"<<(stored==replayed?"true":"false")<<'}';
    }
    const auto captureCases=cases;
    // Every velocity word in both movement paths, and every old wheel-speed
    // word in the decay path. Initial output words and adjacent bytes vary.
    for(unsigned value=0;value<65536;++value) {
        randomizeGuards(); const auto word=static_cast<std::uint16_t>(value);
        checkSpeed({word,0},{randomWord(),randomWord()});
        checkSpeed({word,1},{randomWord(),randomWord()});
        checkSpeed({0x8000,0},{randomWord(),word});
    }
    // Exhaust each of the five damping words independently, with nontrivial
    // fixed values in other fields, through both branches.
    for(unsigned field=0;field<5;++field) for(unsigned value=0;value<65536;++value) {
        std::array<std::uint16_t,5> values{0x8001,0x7fff,0xffff,1,0xfffd};
        values[field]=static_cast<std::uint16_t>(value); randomizeGuards();
        for(unsigned moving=0;moving<2;++moving)
            checkDamping({values[0],values[1],values[2],values[3],values[4],static_cast<std::uint8_t>(moving)},
                         {randomWord(),randomWord()});
    }
    const auto sweepCases=cases-captureCases;
    constexpr std::array<std::uint16_t,24> edges{0,1,2,3,4,7,8,15,16,17,0x7ff,0x800,
        0x801,0x67ff,0x6800,0x6801,0x7fff,0x8000,0x8001,0xbfff,0xc000,0xffef,0xfffe,0xffff};
    // Every movement byte, including positive nonzero values and 0x80/0xff.
    for(unsigned flag=0;flag<256;++flag) for(auto edge:edges) {
        randomizeGuards(); const auto active=static_cast<std::uint8_t>(flag);
        checkSpeed({edge,active},{randomWord(),static_cast<std::uint16_t>(edge^0xffff)});
        checkDamping({edge,static_cast<std::uint16_t>(edge^0xffff),edge,0x8000,edge,active},
                     {randomWord(),randomWord()});
    }
    // Cross signed rounding and wrapped sums at independently varied boundaries.
    for(auto rotation:edges) for(auto target:edges) for(auto yaw:edges) for(unsigned flag=0;flag<2;++flag) {
        randomizeGuards();
        checkDamping({rotation,static_cast<std::uint16_t>(rotation^0xffff),target,
                      static_cast<std::uint16_t>(target^0xffff),yaw,static_cast<std::uint8_t>(flag)},
                     {randomWord(),randomWord()});
    }
    const auto boundaryCases=cases-captureCases-sweepCases;
    for(unsigned n=0;n<100000;++n) {
        randomizeGuards();
        checkSpeed({randomWord(),static_cast<std::uint8_t>(next(randomState))},{randomWord(),randomWord()});
        checkDamping({randomWord(),randomWord(),randomWord(),randomWord(),randomWord(),
                      static_cast<std::uint8_t>(next(randomState))},{randomWord(),randomWord()});
    }
    const auto randomCases=cases-captureCases-sweepCases-boundaryCases;
    // Continue native and original states across active-to-inactive cycles,
    // including long decay runs that must retain the 1..3 residual speed.
    for(unsigned sequence=0;sequence<256;++sequence) {
        randomizeGuards(); scr::WheelSpeedState state{randomWord(),randomWord()};
        for(unsigned step=0;step<256;++step) {
            const auto moving=static_cast<std::uint8_t>(step%64==0 ? sequence : 0);
            state=checkSpeed({randomWord(),moving},state);
        }
    }
    const auto sequentialCases=cases-captureCases-sweepCases-boundaryCases-randomCases;
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"single_word_sweep_cases\":"<<sweepCases
             <<",\"boundary_cases\":"<<boundaryCases<<",\"random_cases\":"<<randomCases
             <<",\"sequential_update_cases\":"<<sequentialCases<<",\"speed_cases\":"<<speedCases
             <<",\"damping_cases\":"<<dampingCases<<",\"total_cases\":"<<cases<<",\"mismatches\":0"
             <<",\"maximum_executed_instructions\":"<<maximumInstructions
             <<",\"writes_confined_to_two_word_outputs_per_call\":true,\"preserved_bytes_checked_per_call\":18"
             <<",\"register_preconditions\":\"none; all general registers randomized\",\"stack_writes\":0}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<'\n'<<error.what()<<'\n'; return 1; }

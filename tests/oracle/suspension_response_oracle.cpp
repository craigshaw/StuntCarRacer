// Execute the original compression/damping prefix and arithmetic callees.
// Stop BEFORE the original countdown call without patching any original bytes.
#include "suspension_response_fixture.h"
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
namespace fixture=scr::test::suspension;
constexpr unsigned load=0xe700, payloadSize=408540, stack=0x7f000, sentinel=0x7e000;
constexpr std::array<fixture::MemoryRange,5> codeRanges{{
    {fixture::entry,fixture::boundary},{0x6180e,0x6181e},{0x622dc,0x62424},
    {0x62424,0x62454},{0x612a2,0x612be},
}};
constexpr std::array<unsigned,8> callSites{0x61c30,0x61d58,0x61e80,0x61f76,0x62306,0x62334,0x6234a,0x6235c};
std::array<std::uint8_t,0x80000> ram{}, nativeRam{};
scr::SuspensionAttenuationTable table{};
bool executing=false, invalid=false, reachedBoundary=false;
unsigned currentPc=0, instructionCount=0, maximumInstructions=0, boundaryHits=0;
unsigned expectedStackAddress=0, expectedStackMask=0, expectedReturn=0, stackBytes=0, calls=0;
std::uint32_t randomState=0x53555331, cpuState=0x36385352;
std::uint64_t cases=0;

std::uint32_t next(std::uint32_t& state) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
bool contains(unsigned a,unsigned n,unsigned begin,unsigned end) { return a>=begin && a<end && n<=end-a; }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
bool readAllowed(unsigned a,unsigned n) {
    if(!contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))) return false;
    if(!executing) return true;
    if(contains(a,n,stack-8,stack+4) || contains(a,n,sentinel,sentinel+8)) return true;
    for(auto range:codeRanges) if(contains(a,n,range.begin,range.end+4)) return true;
    for(auto range:fixture::observedRanges) if(contains(a,n,range.begin,range.end)) return true;
    for(auto range:fixture::inputRanges) if(contains(a,n,range.begin,range.end)) return true;
    return false;
}
void hook(unsigned pc) {
    currentPc=pc;
    if(pc==fixture::boundary) {
        reachedBoundary=true; ++boundaryHits;
        if(m68k_get_reg(nullptr,M68K_REG_SP)!=stack || expectedStackMask) invalid=true;
        // Musashi still fetches one instruction after its hook even when the
        // cycle budget is ended. Redirect that fetch to a test-owned STOP.
        m68k_set_reg(M68K_REG_PC,sentinel); m68k_end_timeslice(); return;
    }
    bool allowed=false;
    for(auto range:codeRanges) allowed|=contains(pc,2,range.begin,range.end);
    if(!allowed || ++instructionCount>600) {
        invalid=true; m68k_set_reg(M68K_REG_PC,sentinel); m68k_end_timeslice(); return;
    }
    const auto call=std::find(callSites.begin(),callSites.end(),pc);
    if(call!=callSites.end()) {
        const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
        const auto expectedSp=unsigned(call-callSites.begin())<4 ? stack : stack-4;
        if(sp!=expectedSp || expectedStackMask) invalid=true;
        expectedStackAddress=sp-4; expectedStackMask=15; expectedReturn=pc+6; ++calls;
    }
}
void runOriginal() {
    executing=false;
    fixture::writeLong(ram.data(),0,stack); fixture::writeLong(ram.data(),4,fixture::entry);
    fixture::writeLong(ram.data(),stack,sentinel);
    fixture::writeWord(ram.data(),sentinel,0x4e72); fixture::writeWord(ram.data(),sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(cpuState));
    m68k_set_reg(M68K_REG_D1,next(cpuState)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(cpuState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,fixture::entry);
    invalid=reachedBoundary=false;
    instructionCount=boundaryHits=expectedStackMask=stackBytes=calls=0;
    executing=true; m68k_execute(40000); executing=false;
    if(invalid || !reachedBoundary || boundaryHits!=1 || calls!=8 || stackBytes!=32 || expectedStackMask
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack)
        throw std::runtime_error("Original execution/access/boundary contract failed, PC="+std::to_string(currentPc));
    maximumInstructions=std::max(maximumInstructions,instructionCount);
}
std::vector<std::uint8_t> observed() {
    std::vector<std::uint8_t> bytes;
    for(auto range:fixture::observedRanges) bytes.insert(bytes.end(),ram.begin()+range.begin,ram.begin()+range.end);
    return bytes;
}
void check(const scr::SuspensionResponseInputs& input) {
    fixture::writeInputs(ram.data(),input);
    for(auto range:fixture::observedRanges)
        std::copy(ram.begin()+range.begin,ram.begin()+range.end,nativeRam.begin()+range.begin);
    auto state=fixture::readState(ram.data());
    scr::updateSuspensionResponse(input,table,state);
    fixture::writeState(nativeRam.data(),state);
    runOriginal();
    for(auto range:fixture::observedRanges) for(auto a=range.begin;a<range.end;++a)
        if(ram[a]!=nativeRam[a]) {
            std::cerr<<"Mismatch case "<<cases<<" address0x"<<std::hex<<a<<std::dec
                     <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
            std::cerr<<"wheel/expected/offset ";
            for(auto v:input.wheelHeight) std::cerr<<v<<' ';
            for(auto v:input.expectedHeight) std::cerr<<v<<' ';
            std::cerr<<input.trackOffsetAdjustment<<'\n';
            throw std::runtime_error("Suspension response mismatch");
        }
    ++cases;
}
void randomizeState() {
    for(auto range:fixture::observedRanges) for(auto a=range.begin;a<range.end;++a) ram[a]=std::uint8_t(next(cpuState));
}
scr::SuspensionResponseInputs baseInput() {
    return {{0x1400,0x800,0},{0,0,0},0,1,0,12,1,0,0,0};
}
void resetState() {
    for(auto range:fixture::observedRanges) std::fill(ram.begin()+range.begin,ram.begin()+range.end,0);
}
void printBytes(const std::vector<std::uint8_t>& bytes) {
    std::cout<<'['; for(std::size_t i=0;i<bytes.size();++i) { if(i) std::cout<<','; std::cout<<unsigned(bytes[i]); } std::cout<<']';
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!readAllowed(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!readAllowed(a,2)) { invalid=true; return 0; } return fixture::readWord(ram.data(),a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);
}
void m68k_write_memory_8(unsigned int a,unsigned int v) {
    if(!executing || std::find(fixture::byteOutputs.begin(),fixture::byteOutputs.end(),a)==fixture::byteOutputs.end()) { invalid=true; return; }
    ram[a]=std::uint8_t(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(executing && expectedStackMask && contains(a,2,expectedStackAddress,expectedStackAddress+4) && !(a&1)) {
        const unsigned offset=a-expectedStackAddress, mask=3u<<offset, shift=8*(2-offset);
        if((expectedStackMask&mask)!=mask || std::uint16_t(v)!=std::uint16_t(expectedReturn>>shift)) { invalid=true; return; }
        expectedStackMask&=~mask; stackBytes+=2; fixture::writeWord(ram.data(),a,std::uint16_t(v)); return;
    }
    if(!executing || std::find(fixture::wordOutputs.begin(),fixture::wordOutputs.end(),a)==fixture::wordOutputs.end()) { invalid=true; return; }
    fixture::writeWord(ram.data(),a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v) {
    if(executing && expectedStackMask==15 && a==expectedStackAddress && v==expectedReturn) {
        expectedStackMask=0; stackBytes+=4; fixture::writeLong(ram.data(),a,v); return;
    }
    if(!executing || std::find(fixture::longOutputs.begin(),fixture::longOutputs.end(),a)==fixture::longOutputs.end()) { invalid=true; return; }
    fixture::writeLong(ram.data(),a,v);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: suspension_response_oracle payload.bin [label chip-ram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Unexpected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+load);
    table=fixture::readAttenuation(ram.data());
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":1398100785,\"captures\":[";
    for(int a=2;a<argc;a+=2) {
        const std::string label=argv[a];
        if(label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[a+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Expected 512KiB capture");
        for(auto range:codeRanges) if(!std::equal(capture.begin()+range.begin,capture.begin()+range.end,payload.begin()+range.begin-load))
            throw std::runtime_error("Capture code differs from original");
        if(!std::equal(table.begin(),table.end(),capture.begin()+fixture::tableAddress))
            throw std::runtime_error("Capture attenuation table differs");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto before=observed(); check(fixture::readInputs(ram.data())); const auto after=observed();
        if(a>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"stored_state_agrees\":"<<(before==after?"true":"false")
                 <<",\"replayed_state_bytes\":"; printBytes(after);
        std::cout<<",\"stored_state_bytes\":"; printBytes(before); std::cout<<'}';
    }
    const auto captureCases=cases;
    // Exhaust every prior-travel word and every signed travel word for each
    // wheel. Randomize all previous state/holes independently for every case.
    for(unsigned field=0;field<6;++field) for(unsigned value=0;value<65536;++value) {
        randomizeState(); auto input=baseInput();
        if(field<3) fixture::writeWord(ram.data(),0x1bd1a+field*2,std::uint16_t(value));
        else input.wheelHeight[field-3]=value<32768 ? value : value|0xffff0000u;
        check(input);
    }
    const auto sweepCases=cases-captureCases;
    // Every counter/limit pair exercises byte wrapping and signed CMP.B.
    for(unsigned counter=0;counter<256;++counter) for(unsigned limit=0;limit<256;++limit) {
        resetState(); auto input=baseInput(); input.wheelHeight={0x1000,0x1000,0x1000};
        input.trackWidthMultiplier=0; input.damageFrameLimit=std::uint8_t(limit);
        ram[0x1bb56]=std::uint8_t(counter); ram[0x1bb4f]=0xfe; ram[0x1bb50]=0xff; check(input);
    }
    const auto counterCases=cases-captureCases-sweepCases;
    constexpr std::array<std::uint32_t,17> travelEdges{0xfffffcff,0xfffffd00,0xfffffd01,0xffffffff,0,1,0x1ff,0x200,
        0x3ff,0x400,0x6ff,0x700,0x11ff,0x1200,0x13ff,0x1400,0x1401};
    constexpr std::array<std::uint16_t,11> previousEdges{0,1,0x1ff,0x200,0x3ff,0x400,0x7fff,0x8000,0x8001,0xff00,0xffff};
    constexpr std::array<std::uint8_t,7> widths{0,1,7,0x7f,0x80,0xfe,0xff};
    for(auto travel:travelEdges) for(auto previous:previousEdges) for(auto width:widths) for(unsigned mode=0;mode<8;++mode) {
        randomizeState(); auto input=baseInput(); input.wheelHeight={travel,travel,travel};
        input.trackWidthMultiplier=width; input.frameThrottleFlag=mode&1 ? 0x80 : 0;
        input.damageFrameLimit=mode&2 ? 0x80 : 0x7f;
        for(unsigned i=0;i<3;++i) {
            fixture::writeWord(ram.data(),0x1bd1a+i*2,previous);
            fixture::writeWord(ram.data(),0x1bd20+i*2,mode&4 ? 0x8000 : 0x1ff);
        }
        check(input);
    }
    const auto boundaryCases=cases-captureCases-sweepCases-counterCases;
    constexpr std::array<std::uint16_t,8> pitches{0,1,0xfff,0x1000,0x1001,0x7fff,0x8000,0xffff};
    constexpr std::array<std::uint8_t,8> tracks{0,1,3,4,6,7,8,255};
    for(auto pitch:pitches) for(auto track:tracks) for(unsigned rotation=0;rotation<256;++rotation) {
        resetState(); auto input=baseInput(); input.wheelHeight={0,0,0}; input.raceStartTimer=0;
        input.pitchAngle=pitch; input.trackId=track; input.rotationSpeedX=std::uint16_t(rotation<<8);
        check(input);
    }
    const auto settlingCases=cases-captureCases-sweepCases-counterCases-boundaryCases;
    for(unsigned chain=0;chain<512;++chain) {
        resetState(); auto input=baseInput(); input.raceStartTimer=0; input.trackWidthMultiplier=std::uint8_t(chain);
        for(unsigned step=0;step<64;++step) {
            for(unsigned i=0;i<3;++i) input.wheelHeight[i]=travelEdges[(step+i*5+chain)%travelEdges.size()];
            input.frameThrottleFlag=step&1 ? 0x80 : 0;
            input.damageFrameLimit=std::uint8_t(chain/2); input.pitchAngle=std::uint16_t(next(randomState));
            check(input); // Original and native state carry to the next update.
        }
    }
    for(unsigned n=0;n<100000;++n) {
        randomizeState(); scr::SuspensionResponseInputs input{};
        for(auto& v:input.wheelHeight) v=next(randomState);
        for(auto& v:input.expectedHeight) v=next(randomState);
        input.trackOffsetAdjustment=next(randomState); input.trackWidthMultiplier=std::uint8_t(next(randomState));
        input.frameThrottleFlag=std::uint8_t(next(randomState)); input.damageFrameLimit=std::uint8_t(next(randomState));
        input.raceStartTimer=std::uint8_t(next(randomState)); input.pitchAngle=std::uint16_t(next(randomState));
        input.trackId=std::uint8_t(next(randomState)); input.rotationSpeedX=std::uint16_t(next(randomState)); check(input);
    }
    std::cout<<"],\"single_word_sweep_cases\":"<<sweepCases<<",\"counter_limit_cases\":"<<counterCases
             <<",\"threshold_cases\":"<<boundaryCases<<",\"settling_cases\":"<<settlingCases
             <<",\"sequential_cases\":32768,\"random_cases\":100000,\"total_cases\":"<<cases
             <<",\"mismatches\":0,\"maximum_instructions\":"<<maximumInstructions
             <<",\"entry_D1_high_byte\":0,\"stop_before\":\"0x62042\",\"original_code_unmodified\":true"
             <<",\"writes_confined\":true,\"stack_checked\":true}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<"\n"<<error.what()<<'\n'; return 1; }

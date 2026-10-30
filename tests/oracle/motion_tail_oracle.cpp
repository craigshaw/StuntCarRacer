// The actual five-call driver tail executes continuously, with no register
// resets between functions and no changes to original instructions or tables.
#include "physics/motion_tail.h"
#include "motion_integration_fixture.h"
#include "motion_transforms_fixture.h"
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
namespace integration=scr::test::integration;
namespace transform=scr::test::motion;
namespace wheel=scr::test::wheelMotion;
using Ram=std::array<std::uint8_t,0x80000>;
Ram ram{},nativeRam{},baseline{};
constexpr unsigned entry=0x618ae,end=0x618ce,stack=0x7f000,sentinel=0x7e000;
struct Range { unsigned begin,end; };
constexpr std::array<Range,7> codeRanges{{
    {entry,end},{0x62138,0x6217a},{0x61b26,0x61b70},{0x61672,0x616d6},
    {0x61344,0x6135a},{0x61adc,0x61b26},{0x61950,0x61ad4},
}};
constexpr std::array<Range,2> tableRanges{{{0x1ec46,0x1ec5c},{0x61ad4,0x61adc}}};
constexpr std::array<unsigned,8> callSites{0x618ae,0x618b4,0x618ba,0x618c0,0x618c6,0x61690,0x616a2,0x616c2};
constexpr std::array<unsigned,16> wordOutputs{0x1bcfc,0x1bd00,0x1bcf0,0x1bcf2,0x1bcf4,
    0x1bd3a,0x1bd3c,0x1bd3e,0x1bcea,0x1bcec,0x1bcee,0x1bce4,0x1bce6,0x1bce8,0x1bcdc,0x1bc42};
constexpr std::array<unsigned,3> longOutputs{0x1bcd8,0x1bcdc,0x1bce0};
std::uint32_t rng=0x5441494c,registerRng=0x36384d54;
std::uint64_t cases=0,totalInstructions=0;
unsigned currentPc=0,instructions=0,maximumInstructions=0,returns=0,calls=0,stackBytes=0;
unsigned pendingStackAddress=0,pendingStackMask=0,pendingReturn=0;
bool executing=false,invalid=false,returned=false;

std::uint32_t next(std::uint32_t& value) { value^=value<<13; value^=value>>17; value^=value<<5; return value; }
std::uint16_t randomWord() { return static_cast<std::uint16_t>(next(rng)); }
std::uint8_t randomByte() { return static_cast<std::uint8_t>(next(rng)); }
bool contains(unsigned a,unsigned n,unsigned begin,unsigned limit) { return a>=begin && a<limit && n<=limit-a; }
bool validMemory(unsigned a,unsigned n) { return contains(a,n,0,unsigned(ram.size())) && (n==1 || !(a&1)); }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void failExecution() { invalid=true; m68k_set_reg(M68K_REG_PC,sentinel); m68k_end_timeslice(); }
void hook(unsigned pc) {
    currentPc=pc;
    if(invalid) { failExecution(); return; }
    if(pc==sentinel) { returned=true; ++returns; m68k_end_timeslice(); return; }
    bool allowed=false;
    for(auto range:codeRanges) allowed|=contains(pc,2,range.begin,range.end);
    if(!allowed || (pc&1) || ++instructions>500) { failExecution(); return; }
    const auto call=std::find(callSites.begin(),callSites.end(),pc);
    if(call!=callSites.end()) {
        const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
        const auto expectedSp=unsigned(call-callSites.begin())<5 ? stack : stack-4;
        if(sp!=expectedSp || pendingStackMask) { failExecution(); return; }
        pendingStackAddress=sp-4; pendingStackMask=15; pendingReturn=pc+6; ++calls;
    }
}
void original() {
    executing=false;
    integration::putLong(ram.data(),0,stack); integration::putLong(ram.data(),4,entry);
    integration::putLong(ram.data(),stack,sentinel);
    integration::putWord(ram.data(),sentinel,0x4e72); integration::putWord(ram.data(),sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(registerRng));
    // Establish the original byte-index/timestep convention once at entry.
    m68k_set_reg(M68K_REG_D1,next(registerRng)&0xffff00ffu);
    m68k_set_reg(M68K_REG_D2,next(registerRng)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(registerRng)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false; instructions=returns=calls=stackBytes=pendingStackMask=0;
    executing=true; m68k_execute(20000); executing=false;
    if(invalid || !returned || returns!=1 || calls!=10 || stackBytes!=40 || pendingStackMask
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Original motion tail execution/access/stack contract failed, PC="+std::to_string(currentPc));
    totalInstructions+=instructions; maximumInstructions=std::max(maximumInstructions,instructions);
}
bool infrastructure(unsigned a) {
    return a<8 || contains(a,1,sentinel,sentinel+4) || contains(a,1,stack-8,stack+4);
}
void check() {
    nativeRam=ram;
    const auto existingInputs=integration::readInputs(ram.data());
    const auto damping=wheel::readDampingInputs(ram.data());
    scr::MotionTailInputs input{existingInputs.worldAcceleration,existingInputs.angularAcceleration[1],
        damping.targetPitchRate,damping.targetRollRate,damping.adjustedYawAngle,damping.wheelMovementActive,
        existingInputs.position.raceCompletionFlag,existingInputs.position.renderingOrderMode};
    scr::MotionTailState state{integration::readState(ram.data()),wheel::readDampingState(ram.data()),
                              existingInputs.position.transformedAngularVelocity};
    scr::updateMotionTail(input,transform::readMatrices(ram.data()),transform::readSelectors(ram.data()),
                          existingInputs.limits,state);
    integration::writeState(nativeRam.data(),state.integration);
    wheel::writeDampingState(nativeRam.data(),state.damping);
    auto transformed=transform::readState(nativeRam.data());
    transformed.transformedAngularVelocity=state.transformedAngularVelocity;
    transform::writeState(nativeRam.data(),transformed);
    original();
    for(unsigned a=0;a<ram.size();++a) if(!infrastructure(a) && ram[a]!=nativeRam[a]) {
        std::cerr<<"Motion tail mismatch case="<<cases<<" address0x"<<std::hex<<a<<std::dec
                 <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
        throw std::runtime_error("Motion tail state mismatch");
    }
    ++cases;
}
void randomize() {
    auto state=integration::readState(ram.data());
    for(unsigned i=0;i<3;++i) {
        state.worldPosition[i]=next(rng); state.angles[i]=randomWord();
        state.worldVelocity[i]=randomWord(); state.rotationVelocity[i]=randomWord();
    }
    state.steepPitchFlag=randomByte(); state.viewportCenterY=randomWord();
    integration::writeState(ram.data(),state);
    auto input=integration::readInputs(ram.data());
    for(unsigned i=0;i<3;++i) {
        input.worldAcceleration[i]=randomWord(); input.angularAcceleration[i]=randomWord();
        input.position.transformedAngularVelocity[i]=randomWord();
    }
    input.position.raceCompletionFlag=randomByte(); input.position.renderingOrderMode=randomByte();
    integration::writeInputs(ram.data(),input); // Retains the original limit table.
    integration::putWord(ram.data(),0x1bd26,randomWord()); integration::putWord(ram.data(),0x1bd28,randomWord());
    integration::putWord(ram.data(),0x1bd36,randomWord()); ram[0x1bb7e]=randomByte();
    auto matrices=transform::readMatrices(ram.data());
    for(auto& coefficient:matrices.words) coefficient=randomWord();
    transform::writeMatrices(ram.data(),matrices);
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!validMemory(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!validMemory(a,2)) { invalid=true; return 0; } return integration::word(ram.data(),a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    if(!validMemory(a,4)) { invalid=true; return 0; } return integration::longword(ram.data(),a);
}
void m68k_write_memory_8(unsigned int a,unsigned int v) {
    if(!executing || a!=0x1bbab) { invalid=true; return; } ram[a]=static_cast<std::uint8_t>(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(executing && pendingStackMask && contains(a,2,pendingStackAddress,pendingStackAddress+4) && !(a&1)) {
        const unsigned offset=a-pendingStackAddress,mask=3u<<offset,shift=8*(2-offset);
        if((pendingStackMask&mask)!=mask || std::uint16_t(v)!=std::uint16_t(pendingReturn>>shift)) { invalid=true; return; }
        pendingStackMask&=~mask; stackBytes+=2; integration::putWord(ram.data(),a,static_cast<std::uint16_t>(v)); return;
    }
    if(!executing || std::find(wordOutputs.begin(),wordOutputs.end(),a)==wordOutputs.end()) { invalid=true; return; }
    integration::putWord(ram.data(),a,static_cast<std::uint16_t>(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v) {
    if(executing && pendingStackMask==15 && a==pendingStackAddress && v==pendingReturn) {
        pendingStackMask=0; stackBytes+=4; integration::putLong(ram.data(),a,v); return;
    }
    if(!executing || std::find(longOutputs.begin(),longOutputs.end(),a)==longOutputs.end()) { invalid=true; return; }
    integration::putLong(ram.data(),a,v);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: motion_tail_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=408540) throw std::runtime_error("Unexpected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);
    baseline=ram;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<rng<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Expected 512KiB capture");
        for(auto range:codeRanges)
            if(!std::equal(payload.begin()+range.begin-0xe700,payload.begin()+range.end-0xe700,capture.begin()+range.begin))
                throw std::runtime_error("Capture code differs from original");
        for(auto range:tableRanges)
            if(!std::equal(payload.begin()+range.begin-0xe700,payload.begin()+range.end-0xe700,capture.begin()+range.begin))
                throw std::runtime_error("Capture table differs from original");
        std::copy(capture.begin(),capture.end(),ram.begin()); baseline=ram; check();
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"cases\":1}";
    }
    const auto captureCases=cases;
    // Test the original positive/negative clamp boundaries, exact equality,
    // extreme words, and all four combinations of special-mode controls.
    const auto limits=integration::readInputs(baseline.data()).limits;
    std::array<std::uint16_t,16> angles{0,1,0x7fff,0x8000};
    for(unsigned i=0;i<4;++i) {
        angles[4+i*3]=static_cast<std::uint16_t>(limits[i]-1);
        angles[5+i*3]=limits[i]; angles[6+i*3]=static_cast<std::uint16_t>(limits[i]+1);
    }
    for(auto angle:angles) for(auto rotation:std::array<std::uint16_t,6>{0,1,0x7fff,0x8000,0x8001,0xffff})
        for(unsigned mode=0;mode<4;++mode) {
            ram=baseline; randomize();
            auto state=integration::readState(ram.data()); state.angles={angle,angle,angle};
            state.rotationVelocity={rotation,rotation,rotation};
            integration::writeState(ram.data(),state);
            scr::TransformMatrices zeroMatrices{}; transform::writeMatrices(ram.data(),zeroMatrices);
            ram[0x1bb75]=mode&1 ? 0x80 : 0; ram[0x1bb9a]=mode&2 ? 0xe0 : 0xdf;
            check();
        }
    const auto boundaryCases=cases-captureCases;
    for(unsigned i=0;i<8192;++i) { ram=baseline; randomize(); check(); }
    const auto randomCases=cases-captureCases-boundaryCases;
    for(unsigned sequence=0;sequence<16;++sequence) {
        ram=baseline; randomize();
        for(unsigned step=0;step<128;++step) {
            ram[0x1bb7e]=step%3 ? 1 : 0;
            ram[0x1bb75]=step&1 ? 0x80 : 0; ram[0x1bb9a]=step&2 ? 0xe0 : 0xdf;
            check();
        }
    }
    const auto sequentialCases=cases-captureCases-boundaryCases-randomCases;
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"boundary_cases\":"<<boundaryCases
             <<",\"random_cases\":"<<randomCases<<",\"sequential_update_cases\":"<<sequentialCases
             <<",\"total_cases\":"<<cases<<",\"mismatches\":0,\"driver_calls_per_case\":5"
             <<",\"nested_matrix_calls_per_case\":5,\"verified_stack_write_bytes_per_case\":40"
             <<",\"whole_ram_compared_except_test_infrastructure\":true,\"registers_reset_between_calls\":false"
             <<",\"not_complete_physics_tick\":true,\"original_driver_start\":\"0x618ae\",\"original_driver_end\":\"0x618ce\""
             <<",\"maximum_instructions\":"<<maximumInstructions<<",\"total_instructions\":"<<totalInstructions<<"}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<'\n'<<error.what()<<'\n'; return 1; }

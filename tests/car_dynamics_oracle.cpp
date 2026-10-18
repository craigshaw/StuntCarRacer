// Original-byte MC68000 oracle for orientation, roll transition, its helper,
// and drag. Native code does not use CPU registers or an emulated RAM image.
#include "car_dynamics_fixture.h"
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
namespace f=scr::test::carDynamics;
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
std::array<std::uint8_t,0x80000> ram{},nativeRam{};
std::uint32_t rng=0x43415244,registerRng=0x36384344,initialD0=0;
std::uint64_t cases=0;
std::array<std::uint64_t,4> routineCases{};
unsigned mode=0,currentPc=0,instructions=0,maximumInstructions=0,wordMask=0,byteWrites=0;
unsigned returns=0,calls=0,stackBytes=0,pendingStackAddress=0,pendingStackMask=0,pendingReturn=0;
bool executing=false,invalid=false,returned=false;
constexpr std::array<unsigned,3> callSites{0x620fa,0x62130,0x62194};
constexpr std::array<std::uint16_t,16> edges{0,1,2,0x7f,0x80,0xff,0x100,0x9ff,
    0xa00,0xa01,0x3fff,0x4000,0x7fff,0x8000,0x8001,0xffff};

std::uint32_t next(std::uint32_t& state) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
std::uint16_t randomWord() { return static_cast<std::uint16_t>(next(rng)); }
std::uint8_t randomByte() { return static_cast<std::uint8_t>(next(rng)); }
bool contains(unsigned a,unsigned n,unsigned begin,unsigned end) { return a>=begin && a<end && n<=end-a; }
bool validRead(unsigned a,unsigned n) { return contains(a,n,0,unsigned(ram.size())) && (n==1 || !(a&1)); }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void stopInvalid() { invalid=true; m68k_set_reg(M68K_REG_PC,sentinel); m68k_end_timeslice(); }
void hook(unsigned pc) {
    currentPc=pc;
    if(invalid) { stopInvalid(); return; }
    if(pc==sentinel) { returned=true; ++returns; m68k_end_timeslice(); return; }
    bool allowed=contains(pc,2,f::entries[mode],f::ends[mode]);
    if(mode<2) allowed|=contains(pc,2,f::entries[2],f::ends[2]);
    if(mode==0) allowed|=contains(pc,2,f::entries[1],f::ends[1]);
    if(!allowed || (pc&1) || ++instructions>120) { stopInvalid(); return; }
    if(std::find(callSites.begin(),callSites.end(),pc)!=callSites.end()) {
        const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
        const auto expectedSp=(mode==0 && pc==0x62194) ? stack-4 : stack;
        if(sp!=expectedSp || pendingStackMask) { stopInvalid(); return; }
        pendingStackAddress=sp-4; pendingStackMask=15; pendingReturn=pc+6; ++calls;
    }
}
void original(unsigned selected) {
    mode=selected; executing=false;
    f::putLong(ram.data(),0,stack); f::putLong(ram.data(),4,f::entries[mode]);
    f::putLong(ram.data(),stack,sentinel); f::putWord(ram.data(),sentinel,0x4e72);
    f::putWord(ram.data(),sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(registerRng));
    initialD0=m68k_get_reg(nullptr,M68K_REG_D0);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(registerRng)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,f::entries[mode]);
    instructions=wordMask=byteWrites=returns=calls=stackBytes=pendingStackMask=0;
    invalid=returned=false; executing=true; m68k_execute(12000); executing=false;
    constexpr std::array<unsigned,4> expectedCalls{3,1,0,0};
    constexpr std::array<unsigned,4> requiredWords{14,2,0,112};
    const auto expectedByteWrites=mode<2 ? 1u : 0u;
    if(invalid || !returned || returns!=1 || calls!=expectedCalls[mode] || stackBytes!=4*calls
       || pendingStackMask || (wordMask&requiredWords[mode])!=requiredWords[mode]
       || byteWrites!=expectedByteWrites || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Original car dynamics execution/access/stack contract failed, PC="+std::to_string(currentPc));
    maximumInstructions=std::max(maximumInstructions,instructions);
}
void randomizeGuards() {
    for(auto range:f::observedRanges) for(auto a=range.begin;a<range.end;++a) ram[a]=randomByte();
}
void check(unsigned selected) {
    for(auto range:f::observedRanges)
        std::copy(ram.begin()+range.begin,ram.begin()+range.end,nativeRam.begin()+range.begin);
    std::uint16_t multiplier=0;
    if(selected==0) {
        auto state=f::readOrientationState(ram.data());
        scr::updateCarOrientation(f::readOrientationInputs(ram.data()),state);
        f::writeOrientationState(nativeRam.data(),state);
    } else if(selected==1) {
        auto state=f::readRollState(ram.data());
        scr::calculateRollAngleTransition(f::readRollInputs(ram.data()),state);
        f::writeRollState(nativeRam.data(),state);
    } else if(selected==2) {
        multiplier=scr::getSteeringMultiplier(f::word(ram.data(),0x1bd42),ram[0x1bb7e]);
    } else {
        auto state=f::readDragState(ram.data());
        scr::updateCollisionDrag(f::readDragInputs(ram.data()),state);
        f::writeDragState(nativeRam.data(),state);
    }
    original(selected);
    for(auto range:f::observedRanges) for(auto a=range.begin;a<range.end;++a)
        if(ram[a]!=nativeRam[a]) {
            std::cerr<<"Car dynamics mismatch case="<<cases<<" routine="<<selected<<" address0x"<<std::hex<<a<<std::dec
                     <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
            throw std::runtime_error("Car dynamics state mismatch");
        }
    if(selected==2 && m68k_get_reg(nullptr,M68K_REG_D0)!=((initialD0&0xffff0000u)|multiplier))
        throw std::runtime_error("Steering multiplier output or preserved D0 high word differs");
    ++cases; ++routineCases[selected];
}
void randomOrientation() {
    randomizeGuards();
    f::writeOrientationInputs(ram.data(),{randomWord(),randomWord(),randomWord(),randomWord(),
        randomWord(),randomWord(),randomWord(),randomWord(),randomByte()});
    f::writeOrientationState(ram.data(),{randomWord(),randomWord(),randomWord(),{randomWord(),randomByte()}});
}
void randomRoll() {
    randomizeGuards(); f::writeRollInputs(ram.data(),{randomWord(),randomWord(),randomWord(),randomWord(),randomByte()});
    f::writeRollState(ram.data(),{randomWord(),randomByte()});
}
scr::CarDragInputs dragBase(unsigned variant) {
    scr::CarDragInputs input{{0x1234,0x8000,0x4567},{0x8001,0xffff,0x7fff},0,0,0,0,0,0,0};
    if(variant==1) { input.wheelMovementActive=1; input.collisionResponseHighByte=3; }
    if(variant==2) { input.wheelMovementActive=1; input.postWreckStateFlag=1; }
    if(variant==3) input.draftingTimer=0x80;
    return input;
}
void randomDrag() {
    randomizeGuards(); scr::CarDragInputs input{};
    for(unsigned i=0;i<3;++i) { input.localVelocity[i]=randomWord(); input.worldVelocity[i]=randomWord(); }
    input.wheelMovementActive=randomByte(); input.collisionResponseHighByte=randomByte();
    input.offTrackStateFlags=randomByte(); input.postWreckStateFlag=randomByte(); input.raceStartTimer=randomByte();
    input.draftingTimer=randomByte(); input.opponentAheadFlag=randomByte(); f::writeDragInputs(ram.data(),input);
    f::writeDragState(ram.data(),{{randomWord(),randomWord(),randomWord()}});
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!validRead(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!validRead(a,2)) { invalid=true; return 0; } return f::word(ram.data(),a);
}
unsigned int m68k_read_memory_32(unsigned int a) { return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2); }
void m68k_write_memory_8(unsigned int a,unsigned int v) {
    if(!executing || mode>1 || a!=f::byteOutput) { invalid=true; return; }
    ++byteWrites; ram[a]=static_cast<std::uint8_t>(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(executing && pendingStackMask && contains(a,2,pendingStackAddress,pendingStackAddress+4) && !(a&1)) {
        const unsigned offset=a-pendingStackAddress,mask=3u<<offset,shift=8*(2-offset);
        if((pendingStackMask&mask)!=mask || std::uint16_t(v)!=std::uint16_t(pendingReturn>>shift)) { invalid=true; return; }
        pendingStackMask&=~mask; stackBytes+=2; f::putWord(ram.data(),a,static_cast<std::uint16_t>(v)); return;
    }
    const auto found=std::find(f::wordOutputs.begin(),f::wordOutputs.end(),a);
    if(!executing || found==f::wordOutputs.end()) { invalid=true; return; }
    const unsigned index=unsigned(found-f::wordOutputs.begin());
    if((mode==0 && index>=4) || (mode==1 && index!=1) || mode==2 || (mode==3 && index<4)) { invalid=true; return; }
    wordMask|=1u<<index; f::putWord(ram.data(),a,static_cast<std::uint16_t>(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v) {
    if(executing && pendingStackMask==15 && a==pendingStackAddress && v==pendingReturn) {
        pendingStackMask=0; stackBytes+=4; f::putLong(ram.data(),a,v); return;
    }
    invalid=true;
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: car_dynamics_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=408540) throw std::runtime_error("Unexpected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<rng<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Expected 512KiB capture");
        for(unsigned routine=0;routine<4;++routine)
            if(!std::equal(payload.begin()+f::entries[routine]-0xe700,payload.begin()+f::ends[routine]-0xe700,
                           capture.begin()+f::entries[routine])) throw std::runtime_error("Capture differs from original code");
        for(unsigned routine=0;routine<4;++routine) { std::copy(capture.begin(),capture.end(),ram.begin()); check(routine); }
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"cases\":4}";
    }
    const auto captureCases=cases;
    for(unsigned field=0;field<9;++field) for(unsigned value=0;value<65536;++value) {
        randomizeGuards();
        std::array<std::uint16_t,9> words{0x8001,0x1234,0xffff,0x7fff,0x4000,0xc001,0xffff,0x1234,0x1234};
        words[field]=static_cast<std::uint16_t>(value);
        f::writeOrientationInputs(ram.data(),{words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],1});
        f::writeOrientationState(ram.data(),{words[8],randomWord(),randomWord(),{randomWord(),randomByte()}}); check(0);
    }
    for(unsigned field=0;field<4;++field) for(unsigned value=0;value<65536;++value) {
        randomizeGuards(); std::array<std::uint16_t,4> words{0x8001,0x7fff,0xffff,0x1234};
        words[field]=static_cast<std::uint16_t>(value); f::writeRollInputs(ram.data(),{words[0],words[1],words[2],words[3],1}); check(1);
    }
    for(unsigned value=0;value<65536;++value) for(unsigned active=0;active<2;++active) {
        randomizeGuards(); f::putWord(ram.data(),0x1bd42,static_cast<std::uint16_t>(value));
        ram[0x1bb7e]=static_cast<std::uint8_t>(active); check(2);
    }
    for(unsigned field=0;field<9;++field) for(unsigned value=0;value<65536;++value) {
        randomizeGuards(); auto input=dragBase(field/3); scr::CarDragState state{{0x8000,0x7fff,0xffff}};
        const auto word=static_cast<std::uint16_t>(value);
        if(field<3) { input.localVelocity={0,0,0}; input.localVelocity[field]=word; }
        else if(field<6) input.worldVelocity[field-3]=word;
        else state.worldAcceleration[field-6]=word;
        f::writeDragInputs(ram.data(),input); f::writeDragState(ram.data(),state); check(3);
    }
    const auto sweepCases=cases-captureCases;
    // Every collision byte under every combination of the six branch flags.
    for(unsigned flags=0;flags<64;++flags) for(unsigned byte=0;byte<256;++byte) {
        randomizeGuards(); auto input=dragBase(0);
        input.wheelMovementActive=flags&1 ? 1 : 0; input.offTrackStateFlags=flags&2 ? 0x80 : 0;
        input.postWreckStateFlag=flags&4 ? 1 : 0; input.raceStartTimer=flags&8 ? 1 : 0;
        input.draftingTimer=flags&16 ? 0x80 : 0; input.opponentAheadFlag=flags&32 ? 0x80 : 0;
        input.collisionResponseHighByte=static_cast<std::uint8_t>(byte);
        f::writeDragInputs(ram.data(),input); check(3);
    }
    for(unsigned byte=0;byte<256;++byte) for(auto edge:edges) {
        randomOrientation(); ram[0x1bb7e]=static_cast<std::uint8_t>(byte);
        f::putWord(ram.data(),0x1bd2a,edge); f::putWord(ram.data(),0x1bd42,edge); check(0);
        randomRoll(); ram[0x1bb7e]=static_cast<std::uint8_t>(byte); f::putWord(ram.data(),0x1bd42,edge); check(1); check(2);
    }
    const auto byteCases=cases-captureCases-sweepCases;
    for(auto first:edges) for(auto second:edges) for(auto third:edges) {
        randomizeGuards();
        f::writeOrientationInputs(ram.data(),{first,second,third,third,first,second,third,third,1});
        f::writeOrientationState(ram.data(),{first,0,0,{0,0}}); check(0); check(1);
        for(unsigned variant=0;variant<4;++variant) {
            auto input=dragBase(variant); input.localVelocity={first,second,third};
            input.worldVelocity={third,first,second}; f::writeDragInputs(ram.data(),input); check(3);
        }
    }
    const auto boundaryCases=cases-captureCases-sweepCases-byteCases;
    for(unsigned i=0;i<100000;++i) { randomOrientation(); check(0); randomRoll(); check(1); randomDrag(); check(3); }
    const auto randomCases=cases-captureCases-sweepCases-byteCases-boundaryCases;
    for(unsigned sequence=0;sequence<64;++sequence) {
        randomOrientation(); randomDrag();
        for(unsigned step=0;step<128;++step) {
            ram[0x1bb7e]=static_cast<std::uint8_t>(step%3); check(0); check(3);
        }
    }
    const auto sequentialCases=cases-captureCases-sweepCases-byteCases-boundaryCases-randomCases;
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"single_word_sweep_cases\":"<<sweepCases
             <<",\"byte_flag_cases\":"<<byteCases<<",\"boundary_cases\":"<<boundaryCases<<",\"random_cases\":"<<randomCases
             <<",\"sequential_update_cases\":"<<sequentialCases<<",\"total_cases\":"<<cases<<",\"mismatches\":0"
             <<",\"orientation_cases\":"<<routineCases[0]<<",\"roll_cases\":"<<routineCases[1]
             <<",\"multiplier_cases\":"<<routineCases[2]<<",\"drag_cases\":"<<routineCases[3]
             <<",\"maximum_instructions\":"<<maximumInstructions<<",\"observed_state_and_guard_bytes\":32"
             <<",\"strict_memory_and_stack_writes\":true,\"register_preconditions\":\"none\",\"helper_D0_high_preserved\":true}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<'\n'<<error.what()<<'\n'; return 1; }

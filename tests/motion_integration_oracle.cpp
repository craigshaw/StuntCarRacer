// Differential execution of original MC68000 integration routines, plus the
// unmodified driver tail that calls velocity then position integration.
#include "motion_integration_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace f=scr::test::integration;
constexpr unsigned base=0xe700,payloadSize=408540,stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t seed=0x4d4f544e,cpuSeed=0x36383035;
enum class Stage : unsigned { WorldVelocity,AngularVelocity,Position,Continuous };
struct Case { f::Inputs inputs; scr::MotionIntegrationState state; Stage stage; };
std::array<std::uint8_t,0x80000> ram{},expectedRam{};
std::array<bool,0x80000> writes{};
std::uint32_t rng=seed,cpuRng=cpuSeed;
std::uint64_t total=0,totalInstructions=0;
std::array<std::uint64_t,4> stageCases{};
std::array<std::uint64_t,7> branches{};
unsigned currentPc=0,instructions=0,maxInstructions=0,minInstructions=1000;
Stage stage=Stage::Position;
bool executing=false,invalid=false,returned=false,recordCounts=true;
std::string accessFailure;

std::uint32_t random32(std::uint32_t& state) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
std::uint32_t next() { return random32(rng); }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
bool contains(unsigned a,unsigned n,unsigned first,unsigned end) { return a>=first&&a<end&&n<=end-a; }
bool validAddress(unsigned a,unsigned n) { return contains(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1)); }
bool hasPosition() { return stage==Stage::Position||stage==Stage::Continuous; }
void fail(const char* reason,unsigned address,unsigned size) {
    invalid=true;
    if(accessFailure.empty()) {
        std::ostringstream text;
        text<<reason<<" at0x"<<std::hex<<address<<" size="<<std::dec<<size<<" pc=0x"<<std::hex<<currentPc;
        accessFailure=text.str();
    }
}
bool permittedPc(unsigned pc) {
    if(stage==Stage::WorldVelocity) return pc>=f::worldEntry&&pc<f::worldEnd;
    if(stage==Stage::AngularVelocity) return pc>=f::angularEntry&&pc<f::angularEnd;
    if(stage==Stage::Position) return pc>=f::positionEntry&&pc<f::positionEnd;
    return (pc>=f::continuousEntry&&pc<f::continuousEnd)||(pc>=f::worldEntry&&pc<f::worldEnd)
        ||(pc>=f::positionEntry&&pc<f::positionEnd);
}
bool permittedRead(unsigned a,unsigned n) {
    if(!validAddress(a,n)) return false;
    if(!executing) return true;
    if(contains(a,n,f::positionEntry,f::positionEnd+4)||contains(a,n,f::worldEntry,f::angularEnd+4)
       ||contains(a,n,f::continuousEntry,f::continuousEnd+4)||contains(a,n,stack-4,stack+4)
       ||contains(a,n,sentinel,sentinel+8)) return true;
    for(auto range:f::stateRanges) if(contains(a,n,range.begin,range.end)) return true;
    for(auto range:f::inputRanges) if(contains(a,n,range.begin,range.end)) return true;
    return false;
}
bool permittedWrite(unsigned a,unsigned n) {
    if(!executing||!validAddress(a,n)) { fail("Invalid write",a,n); return false; }
    if(stage==Stage::Continuous&&(currentPc==0x618c0||currentPc==0x618c6)
       &&contains(a,n,stack-4,stack)) return true;
    bool okay=false;
    if(stage==Stage::WorldVelocity||stage==Stage::Continuous) okay=contains(a,n,0x1bcea,0x1bcf0);
    if(stage==Stage::AngularVelocity) okay=contains(a,n,0x1bcf0,0x1bcf6);
    if(hasPosition()) okay=okay||contains(a,n,0x1bcd8,0x1bcea)||contains(a,n,0x1bcf0,0x1bcf2)
        ||contains(a,n,0x1bcf4,0x1bcf6)||contains(a,n,0x1bc42,0x1bc44)||(a==0x1bbab&&n==1);
    if(!okay) { fail("Write outside selected stage",a,n); return false; }
    for(unsigned i=0;i<n;++i) writes[a+i]=true;
    return true;
}
void hook(unsigned pc) {
    currentPc=pc;
    if(invalid) { m68k_end_timeslice(); return; }
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    if((pc&1)||!permittedPc(pc)||++instructions>150) { fail("Invalid PC/budget",pc,2); m68k_end_timeslice(); return; }
    if(!recordCounts) return;
    switch(pc) {
    case 0x619b2: ++branches[0]; break;
    case 0x61a1e: ++branches[1]; break;
    case 0x61a48: ++branches[2]; break;
    case 0x61a5a: ++branches[3]; break;
    case 0x61a84: ++branches[4]; break;
    case 0x61a96: ++branches[5]; break;
    case 0x61aba: ++branches[6]; break;
    default: break;
    }
}
void requireWritten(unsigned first,unsigned end) {
    for(unsigned a=first;a<end;++a) if(!writes[a]) throw std::runtime_error("Missing required state write");
}
scr::MotionIntegrationState original(const Case& c,std::uint8_t d2High=0) {
    stage=c.stage;
    for(auto range:f::stateRanges) std::fill(writes.begin()+range.begin,writes.begin()+range.end,false);
    f::writeInputs(ram.data(),c.inputs); f::writeState(ram.data(),c.state);
    constexpr std::array<unsigned,4> entries{f::worldEntry,f::angularEntry,f::positionEntry,f::continuousEntry};
    const unsigned entry=entries[static_cast<unsigned>(stage)];
    f::putLong(ram.data(),0,stack); f::putLong(ram.data(),4,entry); f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72); f::putWord(ram.data(),sentinel+2,0x2700);
    executing=false; m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRng));
    m68k_set_reg(M68K_REG_D2,(random32(cpuRng)&0xffff00ffu)|(unsigned(d2High)<<8));
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRng)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    instructions=0; invalid=returned=false; accessFailure.clear(); currentPc=entry; executing=true;
    m68k_execute(12000); executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Motion oracle access/return failure: "+accessFailure+" case="+std::to_string(total));
    if(stage==Stage::WorldVelocity||stage==Stage::Continuous) requireWritten(0x1bcea,0x1bcf0);
    if(stage==Stage::AngularVelocity) requireWritten(0x1bcf0,0x1bcf6);
    if(hasPosition()) { requireWritten(0x1bcd8,0x1bcea); requireWritten(0x1bbab,0x1bbac); requireWritten(0x1bc42,0x1bc44); }
    if(recordCounts) {
        totalInstructions+=instructions; maxInstructions=std::max(maxInstructions,instructions);
        minInstructions=std::min(minInstructions,instructions);
    }
    return f::readState(ram.data());
}
scr::MotionIntegrationState check(const Case& c) {
    original(c);
    auto result=c.state;
    switch(c.stage) {
    case Stage::WorldVelocity: scr::integrateWorldVelocity(c.inputs.worldAcceleration,result); break;
    case Stage::AngularVelocity: scr::integrateAngularVelocity(c.inputs.angularAcceleration,result); break;
    case Stage::Position: scr::updateWorldPosition(c.inputs.position,c.inputs.limits,result); break;
    case Stage::Continuous:
        scr::integrateWorldVelocity(c.inputs.worldAcceleration,result);
        scr::updateWorldPosition(c.inputs.position,c.inputs.limits,result); break;
    }
    f::writeState(expectedRam.data(),result);
    for(auto range:f::stateRanges) for(unsigned a=range.begin;a<range.end;++a) if(ram[a]!=expectedRam[a]) {
        std::cerr<<"Motion mismatch case="<<total<<" stage="<<static_cast<unsigned>(c.stage)<<" address=0x"<<std::hex<<a
                 <<" original="<<unsigned(ram[a])<<" native="<<unsigned(expectedRam[a])<<std::dec<<'\n';
        throw std::runtime_error("Motion differential mismatch");
    }
    ++total; ++stageCases[static_cast<unsigned>(c.stage)]; return result;
}
Case randomCase(const scr::MotionAngleLimits& limits,Stage selected) {
    Case c{}; c.stage=selected; c.inputs.limits=limits;
    for(unsigned i=0;i<3;++i) {
        c.inputs.worldAcceleration[i]=std::uint16_t(next()); c.inputs.angularAcceleration[i]=std::uint16_t(next());
        c.inputs.position.transformedAngularVelocity[i]=std::uint16_t(next());
        c.state.worldPosition[i]=next(); c.state.angles[i]=std::uint16_t(next());
        c.state.worldVelocity[i]=std::uint16_t(next()); c.state.rotationVelocity[i]=std::uint16_t(next());
    }
    c.inputs.position.raceCompletionFlag=std::uint8_t(next()); c.inputs.position.renderingOrderMode=std::uint8_t(next());
    c.state.steepPitchFlag=std::uint8_t(next()); c.state.viewportCenterY=std::uint16_t(next()); return c;
}
bool same(const scr::MotionIntegrationState& a,const scr::MotionIntegrationState& b) {
    return a.worldPosition==b.worldPosition&&a.angles==b.angles&&a.worldVelocity==b.worldVelocity
        &&a.rotationVelocity==b.rotationVelocity&&a.steepPitchFlag==b.steepPitchFlag&&a.viewportCenterY==b.viewportCenterY;
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { if(!permittedRead(a,1)) { fail("Invalid read",a,1); return 0; } return ram[a]; }
unsigned int m68k_read_memory_16(unsigned int a) { if(!permittedRead(a,2)) { fail("Invalid read",a,2); return 0; } return f::word(ram.data(),a); }
unsigned int m68k_read_memory_32(unsigned int a) { if(!permittedRead(a,4)) { fail("Invalid read",a,4); return 0; } return f::longword(ram.data(),a); }
void m68k_write_memory_8(unsigned int a,unsigned int v) { if(permittedWrite(a,1)) ram[a]=std::uint8_t(v); }
void m68k_write_memory_16(unsigned int a,unsigned int v) { if(permittedWrite(a,2)) f::putWord(ram.data(),a,std::uint16_t(v)); }
void m68k_write_memory_32(unsigned int a,unsigned int v) { if(permittedWrite(a,4)) f::putLong(ram.data(),a,v); }
}

int main(int argc,char** argv) try {
    if(argc<2||(argc-2)%2) throw std::runtime_error("Usage: motion_integration_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Expected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);
    const auto limits=f::readInputs(ram.data()).limits;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<seed<<",\"cpu_register_seed\":"<<cpuSeed<<",\"captures\":[";
    for(int arg=2;arg<argc;arg+=2) {
        const std::string label=argv[arg];
        if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[arg+1]);
        if(capture.size()!=ram.size()
           ||!std::equal(payload.begin()+f::positionEntry-base,payload.begin()+f::angularEnd-base,capture.begin()+f::positionEntry)
           ||!std::equal(payload.begin()+f::continuousEntry-base,payload.begin()+f::continuousEnd-base,capture.begin()+f::continuousEntry))
            throw std::runtime_error("Capture code/table identity mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto inputs=f::readInputs(ram.data()); const auto before=f::readState(ram.data());
        if(arg>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"stage_state_unchanged\":[";
        for(unsigned s=0;s<4;++s) {
            const auto result=check({inputs,before,static_cast<Stage>(s)});
            if(s) std::cout<<','; std::cout<<(same(before,result)?"true":"false");
        }
        std::cout<<"]}";
    }
    const auto captureCases=total;
    ram.fill(0); std::copy(payload.begin(),payload.end(),ram.begin()+base);
    const auto sweepStart=total;
    for(unsigned selected=0;selected<2;++selected) for(unsigned axis=0;axis<3;++axis) {
        auto c=randomCase(limits,static_cast<Stage>(selected));
        for(unsigned value=0;value<65536;++value) {
            (selected==0?c.inputs.worldAcceleration:c.inputs.angularAcceleration)[axis]=std::uint16_t(value); check(c);
        }
    }
    const auto accelerationCases=total-sweepStart;
    const auto velocityStart=total;
    for(unsigned selected=0;selected<2;++selected) for(unsigned axis=0;axis<3;++axis) {
        auto c=randomCase(limits,Stage::Position);
        for(unsigned value=0;value<65536;++value) {
            (selected==0?c.state.worldVelocity:c.inputs.position.transformedAngularVelocity)[axis]=std::uint16_t(value); check(c);
        }
    }
    const auto velocityCases=total-velocityStart;
    const auto angleStart=total;
    for(unsigned special=0;special<2;++special) for(unsigned axis:{0u,2u}) {
        auto c=randomCase(limits,Stage::Position); c.inputs.position.transformedAngularVelocity={};
        c.inputs.position.raceCompletionFlag=std::uint8_t(special*128); c.inputs.position.renderingOrderMode=0xe0;
        for(unsigned value=0;value<65536;++value) { c.state.angles[axis]=std::uint16_t(value); check(c); }
    }
    const auto angleCases=total-angleStart;
    const auto heightStart=total;
    auto c=randomCase(limits,Stage::Position); c.state.worldVelocity[1]=0;
    for(unsigned value=0;value<65536;++value) { c.state.worldPosition[1]=(value<<16)|0x1357; check(c); }
    const auto heightCases=total-heightStart;
    const auto flagsStart=total;
    c.inputs.position.transformedAngularVelocity={}; c.state.angles={0x3000,0,0xd000};
    for(unsigned completion=0;completion<256;++completion) for(unsigned mode=0;mode<256;++mode) {
        c.inputs.position.raceCompletionFlag=std::uint8_t(completion); c.inputs.position.renderingOrderMode=std::uint8_t(mode);
        c.state.steepPitchFlag=std::uint8_t(mode); check(c);
    }
    const auto flagsCases=total-flagsStart;
    const auto boundaryStart=total;
    for(unsigned special=0;special<2;++special) for(unsigned axis:{0u,2u})
        for(auto limit:{limits[special],limits[special+2]}) for(int offset:{-1,0,1})
            for(unsigned rotation:{0u,1u,0x7fffu,0x8000u,0xffffu}) for(unsigned rate:{0u,1u,0xffffu,0x8000u,0x7fffu}) {
                c=randomCase(limits,Stage::Position); c.state.angles[axis]=std::uint16_t(int(limit)+offset);
                c.state.rotationVelocity[axis]=std::uint16_t(rotation);
                c.inputs.position.transformedAngularVelocity[axis]=std::uint16_t(rate);
                c.inputs.position.raceCompletionFlag=std::uint8_t(special*128); c.inputs.position.renderingOrderMode=0xe0; check(c);
            }
    constexpr std::array<std::uint32_t,13> positions{0,1,0xffff,0x10000,0x03e7ffff,0x03e80000,0x03e8ffff,
        0x03e90001,0x7fffffff,0x80000000,0xfffeffff,0xffff0000,0xffffffff};
    for(unsigned axis=0;axis<3;++axis) for(auto position:positions)
        for(unsigned velocity:{0u,1u,2u,255u,256u,0x7fffu,0x8000u,0xff00u,0xfffeu,0xffffu}) {
            c=randomCase(limits,Stage::Position); c.state.worldPosition[axis]=position;
            c.state.worldVelocity[axis]=std::uint16_t(velocity); check(c);
        }
    const auto boundaryCases=total-boundaryStart;
    const auto continuousStart=total;
    for(unsigned value=0;value<65536;++value) {
        c=randomCase(limits,Stage::Continuous); c.inputs.worldAcceleration[1]=std::uint16_t(value); check(c);
    }
    const auto continuousSweepCases=total-continuousStart;
    constexpr unsigned randomCases=200000;
    for(unsigned i=0;i<randomCases;++i) check(randomCase(limits,static_cast<Stage>(i%4)));
    const auto sequentialStart=total;
    c=randomCase(limits,Stage::AngularVelocity);
    for(unsigned i=0;i<10000;++i) {
        c.stage=Stage::AngularVelocity; c.state=check(c);
        c.stage=Stage::Continuous; c.state=check(c);
    }
    const auto sequentialCases=total-sequentialStart;
    // Extra caller-contract probes. These deliberately violate the native D2
    // precondition and are counted separately from differential cases.
    recordCounts=false; unsigned probes=0,changedProbes=0;
    for(unsigned selected=0;selected<4;++selected) {
        Case probe{}; probe.stage=static_cast<Stage>(selected); probe.inputs.limits=limits;
        probe.inputs.worldAcceleration={1,2,3}; probe.inputs.angularAcceleration={1,2,3};
        probe.state.worldVelocity={1,2,3}; probe.inputs.position.transformedAngularVelocity={1,2,3};
        const auto baseline=original(probe,0);
        for(unsigned high=0;high<256;++high) {
            const auto result=original(probe,std::uint8_t(high)); ++probes;
            if(!same(result,baseline)) ++changedProbes;
            if((high==0)!=same(result,baseline)) throw std::runtime_error("Unexpected D2 precondition probe result");
        }
    }
    for(auto count:branches) if(!count) throw std::runtime_error("Missing original branch coverage");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"acceleration_word_sweep_cases\":"<<accelerationCases
             <<",\"velocity_word_sweep_cases\":"<<velocityCases<<",\"angle_word_sweep_cases\":"<<angleCases
             <<",\"height_high_word_sweep_cases\":"<<heightCases<<",\"completion_mode_byte_cases\":"<<flagsCases
             <<",\"boundary_cases\":"<<boundaryCases<<",\"continuous_sweep_cases\":"<<continuousSweepCases
             <<",\"random_cases\":"<<randomCases<<",\"sequential_cases\":"<<sequentialCases
             <<",\"total_cases\":"<<total<<",\"mismatches\":0,\"stage_cases\":[";
    for(unsigned i=0;i<4;++i) { if(i) std::cout<<','; std::cout<<stageCases[i]; }
    std::cout<<"],\"stage_order\":[\"world_velocity\",\"angular_velocity\",\"world_position\",\"continuous_original_driver_tail\"]"
             <<",\"d2_high_byte_contract\":0,\"non_native_precondition_probes\":"<<probes
             <<",\"nonzero_d2_high_byte_changes\":"<<changedProbes<<",\"original_code_unmodified\":true"
             <<",\"continuous_original_register_flow\":true,\"instructions\":"<<totalInstructions
             <<",\"minimum_instructions\":"<<minInstructions<<",\"maximum_instructions\":"<<maxInstructions
             <<",\"branch_counts\":{\"height_clamp\":"<<branches[0]<<",\"special_limits\":"<<branches[1]
             <<",\"pitch_clamp\":"<<branches[2]<<",\"pitch_rotation_cancel\":"<<branches[3]
             <<",\"roll_clamp\":"<<branches[4]<<",\"roll_rotation_cancel\":"<<branches[5]
             <<",\"steep_pitch_flag\":"<<branches[6]<<"}}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }

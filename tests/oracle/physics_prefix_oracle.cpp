// Execute the actual physics driver continuously through its first eight calls.
// Stop at the suspension countdown boundary; later driver stages are not run.
#include "physics/physics_prefix.h"
#include "motion_transforms_fixture.h"
#include "wheel_motion_fixture.h"
#include "track_sampling_fixture.h"
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
using scr_test::ChipRam;
namespace response=scr::test::suspension;
namespace motion=scr::test::motion;
namespace wheels=scr::test::wheelMotion;
ChipRam ram{},nativeRam{},captureRam{};
std::array<bool,0x80000> permittedOutput{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t seed=0x524f4144;
std::uint32_t rng=seed;
bool executing=false,invalid=false,returned=false;
unsigned currentPc=0,instructions=0,maxInstructions=0;
std::uint64_t total=0,totalInstructions=0;

std::uint32_t random32() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool inRange(unsigned a,unsigned begin,unsigned end) { return a>=begin&&a<end; }
bool validMemory(unsigned a,unsigned n) {
    return a<ram.size() && n<=ram.size()-a && (n==1 || !(a&1));
}
bool allowedPc(unsigned pc) {
    return inRange(pc,0x6185c,0x6188c) // The actual driver, first eight calls.
        ||inRange(pc,0x61368,0x61618)||inRange(pc,0x61338,0x6135a)
        ||inRange(pc,0x618ce,0x61950)||inRange(pc,0x64d08,0x64d66)
        ||inRange(pc,0x5c1d0,0x5c3da)||inRange(pc,0x5fe56,0x5ff94)
        ||inRange(pc,0x5c0aa,0x5c138)||inRange(pc,0x5c484,0x5c65a)
        ||inRange(pc,0x5c808,0x5c890)||inRange(pc,0x60fbe,0x61012)
        ||inRange(pc,0x61b70,0x62042)||inRange(pc,0x6180e,0x6181e)
        ||inRange(pc,0x622dc,0x62454)||inRange(pc,0x612a2,0x612be);
}
void hook(unsigned pc) {
    currentPc=pc;
    if(invalid) { m68k_end_timeslice(); return; }
    if(pc==response::boundary) {
        returned=true;
        // The core fetches after the hook. Redirect to test STOP so the
        // original boundary's countdown JSR is never executed or patched.
        m68k_set_reg(M68K_REG_PC,sentinel);
        m68k_end_timeslice(); return;
    }
    if((pc&1)||!allowedPc(pc)||++instructions>12000) {
        invalid=true; m68k_end_timeslice();
    }
}
void mark(unsigned begin,unsigned size) {
    std::fill(permittedOutput.begin()+begin,permittedOutput.begin()+begin+size,true);
}
bool validWrite(unsigned a,unsigned n) {
    if(!executing || !validMemory(a,n)) return false;
    if(a>=stack-64 && a+n<=stack) return true;
    for(unsigned i=0;i<n;++i) if(!permittedOutput[a+i]) return false;
    return true;
}
void execute() {
    constexpr unsigned entry=0x6185c;
    executing=false;
    scr_test::writeLong(ram,0,stack); scr_test::writeLong(ram,4,entry);
    scr_test::writeLong(ram,stack,sentinel);
    scr_test::writeWord(ram,sentinel,0x4e72); scr_test::writeWord(ram,sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),random32());
    // Establish the sampler's calling convention ONCE at driver entry.
    // All registers/flags then flow unmodified between the original calls.
    m68k_set_reg(M68K_REG_D1,random32()&0xffff00ffu);
    m68k_set_reg(M68K_REG_D2,random32()&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false; instructions=0; executing=true;
    m68k_execute(300000); executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=(stack-4))
        throw std::runtime_error("Original continuous physics prefix failed: PC="+std::to_string(currentPc));
    totalInstructions+=instructions; maxInstructions=std::max(maxInstructions,instructions);
}
bool testInfrastructure(unsigned a) {
    return a<8 || inRange(a,sentinel,sentinel+4)||inRange(a,stack-64,stack+4);
}
void check(const scr::TrackGeometryData& geometry,const scr::TrigTable& trig,
           const scr::SuspensionAttenuationTable& attenuation) {
    nativeRam=ram;
    scr::PhysicsPrefixInputs prefixInputs{};
    auto& input=prefixInputs.road;
    prefixInputs.motion=motion::readInputs(ram.data());
    input.angles={scr_test::readWord(ram,0x1bce4),scr_test::readWord(ram,0x1bce6),
                  scr_test::readWord(ram,0x1bce8),scr_test::readWord(ram,0x1bd5a)};
    input.worldHeight=scr_test::readLong(ram,0x1bcdc);
    input.track=scr_test::decodeTrackSamplingInputs(ram);
    input.response=response::readInputs(ram.data());
    scr::PhysicsPrefixState prefixState{};
    auto& state=prefixState.road;
    prefixState.motion=motion::readState(ram.data());
    prefixState.wheelSpeed=wheels::readSpeedState(ram.data());
    for(unsigned i=0;i<36;++i) state.matrices.words[i]=scr_test::readWord(ram,0x1c230+i*2);
    state.track=scr_test::decodeTrackSamplingState(ram);
    state.response=response::readState(ram.data());
    scr::updatePhysicsPrefix(prefixInputs,geometry,trig,motion::readSelectors(ram.data()),attenuation,prefixState);

    for(unsigned i=0;i<36;++i) scr_test::writeWord(nativeRam,0x1c230+i*2,state.matrices.words[i]);
    const std::array<scr::WheelPoint,3> wheelPoints{state.wheelPositions.frontLeft,state.wheelPositions.frontRight,state.wheelPositions.rearCenter};
    for(unsigned i=0;i<3;++i) {
        scr_test::writeWord(nativeRam,0x1bd02+2*i,std::uint16_t(wheelPoints[i].x));
        scr_test::writeWord(nativeRam,0x1bd08+2*i,std::uint16_t(wheelPoints[i].y));
    }
    scr_test::encodeTrackSamplingState(nativeRam,state.track);
    scr_test::writeLong(nativeRam,0x1bc94,std::uint32_t(state.expectedHeights.frontLeft));
    scr_test::writeLong(nativeRam,0x1bc98,std::uint32_t(state.expectedHeights.frontRight));
    scr_test::writeLong(nativeRam,0x1bc9c,std::uint32_t(state.expectedHeights.rearCenter));
    scr_test::writeWord(nativeRam,0x1bbf6,state.expectedHeights.pitchSine);
    // Original shared scratch aliases are resolved in call order. In native
    // code each stage keeps descriptive outputs rather than sharing raw RAM.
    motion::writeState(nativeRam.data(),prefixState.motion);
    wheels::writeSpeedState(nativeRam.data(),prefixState.wheelSpeed);
    response::writeState(nativeRam.data(),state.response);

    execute();
    for(unsigned a=0;a<ram.size();++a) if(!testInfrastructure(a)&&ram[a]!=nativeRam[a]) {
        std::cerr<<"Physics prefix mismatch case="<<total<<" address=0x"<<std::hex<<a<<std::dec
                 <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a]);
        throw std::runtime_error("Physics prefix state mismatch");
    }
    ++total;
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!validMemory(a,1)) { invalid=true; return 0; } return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!validMemory(a,2)) { invalid=true; return 0; } return scr_test::readWord(ram,a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    if(!validMemory(a,4)) { invalid=true; return 0; } return scr_test::readLong(ram,a);
}
void m68k_write_memory_8(unsigned int a,unsigned int v) {
    if(!validWrite(a,1)) { invalid=true; return; } ram[a]=std::uint8_t(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(!validWrite(a,2)) { invalid=true; return; } scr_test::writeWord(ram,a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v) {
    if(!validWrite(a,4)) { invalid=true; return; } scr_test::writeLong(ram,a,v);
}
}

int main(int argc,char** argv) try {
    if(argc<4 || (argc-2)%2) throw std::runtime_error("Usage: physics_prefix_oracle payload.bin label chipram.bin [...]");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=408540) throw std::runtime_error("Unexpected payload size");
    mark(0x1bd2c,2); mark(0x1bd30,2); mark(0x1bd0e,6);
    for(auto a:wheels::speedOutputs) mark(a,2);
    mark(0x1c230,72); mark(0x1bd02,12); mark(0x1bc94,12); mark(0x1bbf6,2);
    for(auto a:scr_test::trackSamplingOutputBytes()) mark(a,1);
    for(auto range:response::observedRanges) mark(range.begin,range.end-range.begin);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":"<<seed<<",\"captures\":[";
    unsigned validCaptures=0,skippedCaptures=0;
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Expected 512K capture");
        for(auto range:std::array<std::array<unsigned,2>,20>{{
            {0x6185c,0x6188c},{0x6158c,0x61618},{0x61338,0x6135a},
            {0x60fbe,0x61012},{0x1ec46,0x1ec5c},
            {0x61368,0x6158c},{0x618ce,0x61950},{0x64d08,0x64d66},{0x1ca42,0x1cc46},
            {0x61b70,0x61bcc},{0x5c1d0,0x5c3da},{0x5fe56,0x5ff94},{0x5c0aa,0x5c138},
            {0x5c484,0x5c65a},{0x5c808,0x5c890},{0x61bcc,0x62042},{0x6180e,0x6181e},
            {0x622dc,0x62454},{0x612a2,0x612be},{0x1eeca,0x1ef4a}}})
            if(!std::equal(payload.begin()+range[0]-0xe700,payload.begin()+range[1]-0xe700,
                           capture.begin()+range[0]))
                throw std::runtime_error("Capture differs from original code/table in continuous prefix");
        std::copy(capture.begin(),capture.end(),captureRam.begin()); ram=captureRam;
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<'"';
        const auto geometry=scr_test::decodeTrackGeometry(ram);
        if(geometry.segmentCount==0) {
            ++skippedCaptures;
            std::cout<<",\"skipped\":\"No loaded track: segment count is zero\"}"; continue;
        }
        if(geometry.segmentCount>100||ram[0x1bb1c]>=geometry.segmentCount)
            throw std::runtime_error("Invalid loaded track metadata");
        scr::TrigTable trig{};
        for(unsigned i=0;i<trig.size();++i) trig[i]=scr_test::readWord(ram,0x1ca42+i*2);
        const auto attenuation=response::readAttenuation(ram.data());
        check(geometry,trig,attenuation);
        for(unsigned i=0;i<256;++i) {
            scr_test::writeWord(ram,0x1bce4,std::uint16_t(scr_test::readWord(ram,0x1bce4)+3));
            scr_test::writeWord(ram,0x1bce6,std::uint16_t(scr_test::readWord(ram,0x1bce6)+127));
            scr_test::writeWord(ram,0x1bce8,std::uint16_t(scr_test::readWord(ram,0x1bce8)-2));
            scr_test::writeWord(ram,0x1bb10,std::uint16_t(scr_test::readWord(ram,0x1bb10)+128));
            ram[0x1bb1c]=std::uint8_t(i%geometry.segmentCount);
            check(geometry,trig,attenuation);
        }
        for(unsigned i=0;i<1024;++i) {
            ram=captureRam;
            for(auto a:{0x1bce4u,0x1bce6u,0x1bce8u,0x1bd5au,0x1bc5eu,0x1bb10u})
                scr_test::writeWord(ram,a,std::uint16_t(random32()));
            scr_test::writeLong(ram,0x1bcdc,random32());
            for(auto a:{0x1bceau,0x1bcecu,0x1bceeu,0x1bc62u,0x1bd2eu})
                scr_test::writeWord(ram,a,std::uint16_t(random32()));
            ram[0x1bb1c]=std::uint8_t(random32()%geometry.segmentCount);
            ram[0xff]=std::uint8_t(random32()); ram[0x1bd5c]=std::uint8_t(random32());
            for(auto range:response::observedRanges) for(unsigned a=range.begin;a<range.end;++a)
                ram[a]=std::uint8_t(random32());
            check(geometry,trig,attenuation);
        }
        ++validCaptures;
        std::cout<<",\"segment_count\":"<<unsigned(geometry.segmentCount)<<",\"cases\":1281}";
    }
    if(!validCaptures) throw std::runtime_error("At least one capture with a loaded track is required");
    std::cout<<"],\"total_cases\":"<<total<<",\"mismatches\":0,\"valid_track_captures\":"<<validCaptures
             <<",\"skipped_menu_captures\":"<<skippedCaptures<<",\"continuous_driver_calls_per_case\":8"
             <<",\"unmodified_original_code\":true,\"whole_ram_compared_except_test_infrastructure\":true"
             <<",\"max_instructions\":"<<maxInstructions<<",\"total_instructions\":"<<totalInstructions
             <<",\"not_complete_physics_tick\":true,\"registers_reset_between_calls\":false,\"final_prng_state\":"<<rng<<"}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<'\n'<<error.what()<<'\n'; return 1; }

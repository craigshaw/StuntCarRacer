#include "suspension_effects_fixture.h"
#include "audio_playback_fixture.h"
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
namespace f=scr::test::effects;
namespace audio=scr::test::audio;
constexpr unsigned base=0xe700,entry=0x62042,stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t seed=0x45464658,cpuSeed=0x36383036;
struct Case {scr::SuspensionEffectsInputs inputs; scr::SuspensionEffectsState state;
             scr::AudioSampleData audioData; scr::AudioPlaybackState audioState;};
std::array<std::uint8_t,0x80000> ram{},expected{};
std::array<bool,0x80000> readable{},writable{};
std::vector<scr::AudioRegisterWrite> registerWrites;
std::vector<std::uint8_t> sampleIds;
std::uint32_t rng=seed,cpuRng=cpuSeed;
std::uint64_t total=0,totalInstructions=0,audioRequests=0;
std::array<std::uint64_t,14> branches{};
unsigned currentPc=0,instructions=0,maxInstructions=0;
bool executing=false,invalid=false,returned=false;
std::string failure;
std::uint32_t random32(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint32_t next(){return random32(rng);}
std::vector<std::uint8_t> readFile(const char* p){std::ifstream s(p,std::ios::binary);if(!s)throw std::runtime_error("Cannot read fixture");return {std::istreambuf_iterator<char>(s),std::istreambuf_iterator<char>()};}
bool contains(unsigned a,unsigned n,unsigned first,unsigned end){return a>=first&&a<end&&n<=end-a;}
bool valid(unsigned a,unsigned n){return contains(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(currentPc);}
void mark(std::array<bool,0x80000>& mask,unsigned a,unsigned n){std::fill(mask.begin()+a,mask.begin()+a+n,true);}
bool allowedPc(unsigned pc){
    if(pc>=audio::entry&&pc<audio::end)return true;
    for(auto range:f::codeRanges)if(pc>=range.begin&&pc<range.end)return true;
    return false;
}
void hook(unsigned pc){
    currentPc=pc;if(invalid){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    if((pc&1)||!allowedPc(pc)||++instructions>400){fail("Invalid instruction/budget",pc);m68k_end_timeslice();return;}
    if(pc==audio::entry)sampleIds.push_back(std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D0)&7));
    constexpr std::array<unsigned,14> points{0x5b340,0x5b376,0x5b398,0x5b402,0x5b450,0x5b3b0,0x58794,
        0x63e48,0x63ece,0x620a6,0x62080,0x6208e,0x5b496,0x5b4de};
    for(unsigned i=0;i<points.size();++i)if(pc==points[i])++branches[i];
}
bool permittedRead(unsigned a,unsigned n){
    if(!valid(a,n))return false;if(!executing)return true;
    if(contains(a,n,stack-64,stack+4)||contains(a,n,sentinel,sentinel+8))return true;
    for(unsigned i=0;i<n;++i)if(!readable[a+i])return false;return true;
}
bool permittedWrite(unsigned a,unsigned n){
    if(!executing||!valid(a,n))return false;
    if(contains(a,n,stack-64,stack))return true;
    for(unsigned i=0;i<n;++i)if(!writable[a+i])return false;return true;
}
void hardwareWrite(unsigned a,std::uint16_t v){
    if(!executing||currentPc<audio::entry||currentPc>=audio::end||(a&1)||a<0xdff000||a>=0xdff200){fail("Unexpected MMIO write",a);return;}
    registerWrites.push_back({a,v});
}
void initializeMasks(){
    for(auto r:f::codeRanges)mark(readable,r.begin,r.end-r.begin+4);
    mark(readable,audio::entry,audio::end-audio::entry+4);
    for(auto a:f::inputWords)mark(readable,a,2);for(auto a:f::inputBytes)mark(readable,a,1);
    for(auto a:f::stateWords){mark(readable,a,2);mark(writable,a,2);}
    for(auto a:f::stateBytes){mark(readable,a,1);mark(writable,a,1);}
    mark(readable,f::seedAddress,4);mark(writable,f::seedAddress,4);
    mark(readable,audio::parameters,128);mark(readable,audio::channels,16);mark(readable,audio::guard,2);
    for(unsigned i=0;i<4;++i)mark(writable,audio::channels+i*4,2);mark(writable,audio::guard,2);
}
void original(const Case& c){
    audio::writeData(ram.data(),c.audioData);audio::writeState(ram.data(),c.audioState);
    f::writeInputs(ram.data(),c.inputs);f::writeState(ram.data(),c.state);
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRng));
    m68k_set_reg(M68K_REG_D2,random32(cpuRng)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRng)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;instructions=0;failure.clear();registerWrites.clear();sampleIds.clear();currentPc=entry;executing=true;
    m68k_execute(30000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Effects execution failure "+failure+" case="+std::to_string(total));
    totalInstructions+=instructions;maxInstructions=std::max(maxInstructions,instructions);
}
scr::SuspensionEffectsState check(const Case& c){
    original(c);
    auto state=c.state;const auto requests=scr::runSuspensionEffects(c.inputs,state);
    auto data=c.audioData;auto playback=c.audioState;
    data.samples[3].volume=std::uint16_t((data.samples[3].volume&0xff00u)|state.impactVolume);
    std::vector<scr::AudioRegisterWrite> expectedWrites;
    for(unsigned i=0;i<requests.count;++i){const auto writes=scr::requestSample(requests.ids[i],data,playback);expectedWrites.insert(expectedWrites.end(),writes.begin(),writes.end());}
    f::writeState(expected.data(),state);audio::writeState(expected.data(),playback);
    for(auto a:f::stateWords)for(unsigned i=0;i<2;++i)if(ram[a+i]!=expected[a+i]){
        std::cerr<<"Effects mismatch case="<<total<<" timer="<<unsigned(c.state.race.timer)<<" address=0x"<<std::hex<<a+i<<std::dec<<" original="<<unsigned(ram[a+i])<<" native="<<unsigned(expected[a+i])<<'\n';
        throw std::runtime_error("Effects word mismatch");}
    for(auto a:f::stateBytes)if(ram[a]!=expected[a])throw std::runtime_error("Effects byte mismatch at"+std::to_string(a)+" case="+std::to_string(total));
    for(unsigned a=f::seedAddress;a<f::seedAddress+4;++a)if(ram[a]!=expected[a])throw std::runtime_error("Random seed mismatch");
    const auto actualAudio=audio::readState(ram.data());
    if(actualAudio.completedInterrupts!=playback.completedInterrupts||actualAudio.dmaEnableGuard!=playback.dmaEnableGuard)
        throw std::runtime_error("Effects audio state mismatch");
    if(sampleIds.size()!=requests.count||registerWrites.size()!=expectedWrites.size())throw std::runtime_error("Audio request/effect count mismatch");
    for(unsigned i=0;i<requests.count;++i)if(sampleIds[i]!=requests.ids[i])throw std::runtime_error("Audio request order mismatch");
    for(unsigned i=0;i<expectedWrites.size();++i)if(registerWrites[i].address!=expectedWrites[i].address||registerWrites[i].value!=expectedWrites[i].value)
        throw std::runtime_error("Ordered Paula write mismatch");
    audioRequests+=requests.count;++total;return state;
}
Case randomCase(){
    Case c{};auto& i=c.inputs;auto& s=c.state;auto& r=s.race;auto& o=s.collision;
    i={std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint16_t(next())};
    r={std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),next(),std::uint8_t(next())};
    o.active=std::uint8_t(next());o.cooldown=std::uint8_t(next());o.opponentSpeed=std::uint16_t(next());
    for(unsigned axis=0;axis<3;++axis){o.wheelVelocities[axis]=std::uint16_t(next());o.forces[axis]=std::uint16_t(next());s.springAngles[axis]=std::uint16_t(next());}
    s.savedYawSpring=std::uint16_t(next());s.impactCooldown=std::uint8_t(next());s.impactVolume=std::uint8_t(next());
    for(auto& sample:c.audioData.samples)sample={next(),next(),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()%4)};
    for(auto& mask:c.audioData.channelMasks)mask=std::uint16_t(next());
    for(auto& counter:c.audioState.completedInterrupts)counter=std::uint16_t(next());c.audioState.dmaEnableGuard=std::uint16_t(next());return c;
}
void quiet(Case& c){c.state.collision.active=0;c.inputs.hardImpactCount=0;}
}

extern "C"{
unsigned int m68k_read_memory_8(unsigned int a){if(!permittedRead(a,1)){fail("Invalid read",a);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!permittedRead(a,2)){fail("Invalid read",a);return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!permittedRead(a,4)){fail("Invalid read",a);return 0;}return f::longword(ram.data(),a);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!permittedWrite(a,1)){fail("Invalid write",a);return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a>=0xdff000){hardwareWrite(a,std::uint16_t(v));return;}if(!permittedWrite(a,2)){fail("Invalid write",a);return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(a>=0xdff000){hardwareWrite(a,std::uint16_t(v>>16));hardwareWrite(a+2,std::uint16_t(v));return;}if(!permittedWrite(a,4)){fail("Invalid write",a);return;}f::putLong(ram.data(),a,v);}
}

int main(int argc,char** argv)try{
    if(argc<2||(argc-2)%2)throw std::runtime_error("Usage: suspension_effects_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);initializeMasks();
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":"<<seed<<",\"cpu_register_seed\":"<<cpuSeed<<",\"captures\":[";
    for(int arg=2;arg<argc;arg+=2){
        const std::string label=argv[arg];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[arg+1]);if(capture.size()!=ram.size())throw std::runtime_error("Capture size mismatch");
        for(auto range:f::codeRanges)if(!std::equal(payload.begin()+range.begin-base,payload.begin()+range.end-base,capture.begin()+range.begin))throw std::runtime_error("Capture code mismatch");
        if(!std::equal(payload.begin()+audio::entry-base,payload.begin()+audio::end-base,capture.begin()+audio::entry))throw std::runtime_error("Capture audio code mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        Case c{f::readInputs(ram.data()),f::readState(ram.data()),audio::readData(ram.data()),audio::readState(ram.data())};const auto result=check(c);
        if(arg>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\",\"timer_before\":"<<unsigned(c.state.race.timer)<<",\"timer_after\":"<<unsigned(result.race.timer)<<"}";
    }
    const auto captureCases=total;ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+base);
    auto c=randomCase();quiet(c);const auto timerStart=total;
    for(unsigned timer=0;timer<256;++timer)for(unsigned side=0;side<256;++side)for(unsigned complete:{0u,1u,0x80u,0xffu}){
        c.state.race.timer=std::uint8_t(timer);c.state.race.trackSide=std::uint8_t(side);c.state.race.complete=std::uint8_t(complete);
        c.inputs.frameThrottle=std::uint8_t(side);c.inputs.raceActive=std::uint8_t(complete&1);check(c);}
    const auto timerCases=total-timerStart;const auto heightStart=total;
    for(unsigned timer:{0xe4u,0xe5u,0x80u})for(unsigned side:{0u,0x80u}){
        c=randomCase();quiet(c);c.state.race.timer=std::uint8_t(timer);c.state.race.trackSide=std::uint8_t(side);
        c.state.race.roll=std::uint16_t(side?0xf000:0x1000);c.inputs.trackSurfaceHeight=0;
        for(unsigned height=0;height<65536;++height){c.inputs.carHeight=std::uint16_t(height);check(c);}}
    const auto heightCases=total-heightStart;const auto rollStart=total;
    for(unsigned side:{0u,0x80u}){c=randomCase();quiet(c);c.state.race.timer=0xe4;c.state.race.trackSide=std::uint8_t(side);
        for(unsigned roll=0;roll<65536;++roll){c.state.race.roll=std::uint16_t(roll);check(c);}}
    const auto rollCases=total-rollStart;const auto lateralStart=total;
    c=randomCase();quiet(c);c.state.race.timer=0x80;
    for(unsigned lateral=0;lateral<65536;++lateral){c.inputs.lateralOffset=std::uint16_t(lateral);check(c);}
    const auto lateralCases=total-lateralStart;const auto collisionStart=total;
    c=randomCase();c.state.race.timer=0;c.inputs.hardImpactCount=0;c.state.collision.active=1;c.state.collision.cooldown=2;
    for(unsigned axis=0;axis<3;++axis)for(unsigned force=0;force<65536;++force){c.state.collision.forces[axis]=std::uint16_t(force);check(c);}
    for(unsigned speed=0;speed<65536;++speed){c.state.collision.opponentSpeed=std::uint16_t(speed);check(c);}
    const auto collisionCases=total-collisionStart;const auto volumeStart=total;
    c=randomCase();c.state.race.timer=0;c.state.collision.active=0;c.inputs.hardImpactCount=1;
    for(unsigned volume=0;volume<256;++volume)for(unsigned cooldown=0;cooldown<256;++cooldown){
        c.inputs.maxCompressionVelocity=std::uint16_t((volume<<8)|0x5a);c.state.impactCooldown=std::uint8_t(cooldown);check(c);}
    const auto volumeCases=total-volumeStart;const auto swapStart=total;
    c=randomCase();quiet(c);c.state.race.timer=0xfa;c.state.race.complete=0x80;c.inputs.playerSegment=17;c.inputs.opponentSegment=17;
    for(unsigned mode:{0u,1u,0x80u,0xffu})for(unsigned sync=0;sync<256;++sync)for(unsigned side=0;side<256;++side){
        c.inputs.networkMode=std::uint8_t(mode);c.inputs.multiplayerSync=std::uint8_t(sync);c.state.race.trackSide=std::uint8_t(side);check(c);}
    const auto swapCases=total-swapStart;
    constexpr unsigned randomCases=150000;
    for(unsigned i=0;i<randomCases;++i){c=randomCase();if(i%5==0)c.state.race.timer=0xe4;if(i%7==0)c.state.collision.cooldown=0;if(i%11==0)c.state.impactCooldown=0;check(c);}
    const auto sequenceStart=total;
    for(unsigned side:{0u,0x80u}){c=randomCase();quiet(c);c.state.race={};c.state.race.timer=0xff;c.state.race.trackSide=std::uint8_t(side);
        c.inputs.frameThrottle=0;c.inputs.networkMode=0;c.inputs.carHeight=0x0400;c.inputs.trackSurfaceHeight=0;c.inputs.selectedTrack=0x80;c.inputs.raceActive=0;
        for(unsigned i=0;i<512;++i)c.state=check(c);}
    const auto sequentialCases=total-sequenceStart;
    for(auto count:branches)if(!count)throw std::runtime_error("Missing effects branch coverage");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"timer_side_complete_cases\":"<<timerCases<<",\"height_word_cases\":"<<heightCases
        <<",\"roll_word_cases\":"<<rollCases<<",\"lateral_word_cases\":"<<lateralCases<<",\"collision_word_cases\":"<<collisionCases
        <<",\"volume_cooldown_cases\":"<<volumeCases<<",\"multiplayer_swap_cases\":"<<swapCases<<",\"random_cases\":"<<randomCases
        <<",\"sequential_cases\":"<<sequentialCases<<",\"total_cases\":"<<total<<",\"mismatches\":0,\"audio_requests\":"<<audioRequests
        <<",\"original_play_sample_executed\":true,\"ordered_paula_writes_compared\":true,\"maximum_instructions\":"<<maxInstructions
        <<",\"total_instructions\":"<<totalInstructions<<",\"entry_d2_high_byte\":0,\"branches\":[";
    for(unsigned i=0;i<branches.size();++i){if(i)std::cout<<',';std::cout<<branches[i];}std::cout<<"]}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

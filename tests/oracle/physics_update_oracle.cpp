// Execute the untouched original driver from its entry through its final RTS.
// No hooks replace callees, reset registers between stages, or skip audio.
#include "physics_update_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fx=scr::test::physics;
namespace audio=scr::test::audio;
namespace effects=scr::test::effects;
namespace steering=scr::test::steering;
namespace motion=scr::test::motion;
namespace wheels=scr::test::wheelMotion;
namespace integration=scr::test::integration;
namespace response=scr::test::suspension;
using Ram=fx::Ram;
Ram ram{},nativeRam{},captureRam{};
std::array<bool,0x80000> permittedOutput{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000,entry=0x6185c;
constexpr std::uint32_t seed=0x50485953;
std::uint32_t rng=seed;
bool executing=false,invalid=false,returned=false;
unsigned currentPc=0,instructions=0,maxInstructions=0,badAddress=0;
std::uint64_t total=0,totalInstructions=0,audioRequests=0;
std::map<unsigned,std::uint64_t> coverage;
std::vector<scr::AudioRegisterWrite> writes;
const std::vector<std::array<unsigned,2>> codeRanges{
    {0x6185c,0x61ad4},{0x61adc,0x620b6},{0x620b8,0x62454},
    {0x61368,0x616d6},{0x61338,0x6135a},{0x64d08,0x64d66},
    {0x5c1d0,0x5c3da},{0x5fe56,0x5ff94},{0x5c0aa,0x5c138},
    {0x5c484,0x5c65a},{0x5c808,0x5c890},{0x60fbe,0x6125a},
    {0x6180e,0x6181e},{0x612a2,0x612be},{0x5b32e,0x5b506},
    {0x58758,0x5879e},{0x62574,0x625a2},{0x5e4ec,0x5e502},
    {0x63e2e,0x63ede},{0xf362,0xf3ee}};
const std::vector<std::array<unsigned,2>> tableRanges{
    {0x1ec46,0x1ec5c},{0x1ca42,0x1cc46},{0x1eeca,0x1ef4a},
    {0x61ad4,0x61adc},{0x6125a,0x61260}};
std::uint32_t random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
bool inRange(unsigned a,unsigned b,unsigned e){return a>=b&&a<e;}
bool validMemory(unsigned a,unsigned n){return a<ram.size()&&n<=ram.size()-a&&(n==1||!(a&1));}
std::vector<std::uint8_t> readFile(const char* p){
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error(std::string("Cannot read ")+p);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void hook(unsigned pc){
    currentPc=pc;
    if(invalid){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    if((pc&1)||!std::any_of(codeRanges.begin(),codeRanges.end(),[pc](auto r){return inRange(pc,r[0],r[1]);})||++instructions>16000){
        invalid=true;m68k_end_timeslice();return;
    }
    if(pc==0x61896||pc==0x618c0||pc==0xf362||pc==0x63e2e||pc==0x5b32e||pc==0x58758||pc==0x62574||pc==0x5e4ec)
        ++coverage[pc];
}
void mark(unsigned a,unsigned n){std::fill(permittedOutput.begin()+a,permittedOutput.begin()+a+n,true);}
bool validWrite(unsigned a,unsigned n){
    if(!executing||!validMemory(a,n))return false;
    if(a>=stack-64&&a+n<=stack)return true;
    for(unsigned i=0;i<n;++i)if(!permittedOutput[a+i])return false;
    return true;
}
bool mmio(unsigned a){return a==0xdff09a||a==0xdff096||a==0xdff09c||
    (a>=0xdff0a0&&a<=0xdff0d8&&((a-0xdff0a0)%16)<=8&&!(a&1));}
void execute(){
    executing=false;audio::putLong(ram.data(),0,stack);audio::putLong(ram.data(),4,entry);
    audio::putLong(ram.data(),stack,sentinel);audio::putWord(ram.data(),sentinel,0x4e72);audio::putWord(ram.data(),sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),random32());
    // Sampler/countdown byte-index preconditions, established once at entry.
    m68k_set_reg(M68K_REG_D1,random32()&0xffff00ffu);m68k_set_reg(M68K_REG_D2,random32()&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;instructions=badAddress=0;writes.clear();executing=true;
    m68k_execute(600000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4){
        std::cerr<<"Full original driver failed case="<<total<<" PC=0x"<<std::hex<<currentPc<<" memory=0x"<<badAddress<<std::dec<<'\n';
        throw std::runtime_error("Original driver did not return normally");
    }
    totalInstructions+=instructions;maxInstructions=std::max(maxInstructions,instructions);
}
bool infrastructure(unsigned a){return a<8||inRange(a,sentinel,sentinel+4)||inRange(a,stack-64,stack+4);}
void check(const scr::PhysicsData& data,scr::PhysicsState& nativeState){
    // Inputs come from the separately retained native RAM mirror, never from
    // original outputs. The typed state itself persists through sequence tests.
    const auto events=scr::updateGamePhysics(fx::readInputs(nativeRam),data,nativeState);
    fx::writeState(nativeRam,nativeState);execute();
    for(unsigned a=0;a<ram.size();++a)if(!infrastructure(a)&&ram[a]!=nativeRam[a]){
        std::cerr<<"Physics mismatch case="<<total<<" address=0x"<<std::hex<<a<<std::dec
                 <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
        throw std::runtime_error("Whole physics state mismatch");
    }
    if(events.size()!=writes.size())throw std::runtime_error("Audio write count mismatch");
    for(unsigned i=0;i<events.size();++i)if(events[i].address!=writes[i].address||events[i].value!=writes[i].value)
        throw std::runtime_error("Ordered audio register write mismatch");
    audioRequests+=writes.size()/10;++total;
}
void single(const scr::PhysicsData& data){nativeRam=ram;auto s=fx::readState(nativeRam);check(data,s);}
void randomize(unsigned segmentCount){
    // Preserve all code and assets; randomize only documented state and inputs.
    for(unsigned a=0;a<ram.size();++a)if(permittedOutput[a])ram[a]=std::uint8_t(random32());
    for(auto a:{0x1bce4u,0x1bce6u,0x1bce8u,0x1bd5au,0x1bc5eu,0x1bb10u,0x1bd2eu,
                0x1bcd0u,0x1bc60u,0x1bc5cu})audio::putWord(ram.data(),a,std::uint16_t(random32()));
    for(auto a:{0x1bca0u,0x1bcd8u,0x1bcdcu,0x1bce0u})audio::putLong(ram.data(),a,random32());
    for(auto a:{0xffu,0x1bb0au,0x1bc33u,0x1bb01u,0x63ce2u,0x1ca33u,0x1bb72u,
                0x1bbcdu,0x57c3cu,0x1bbe0u,0x1ca22u,0x1bb70u,0x1bb75u,0x1bbc7u,0x1bbb8u})ram[a]=std::uint8_t(random32());
    ram[0x1bb1c]=std::uint8_t(random32()%segmentCount);ram[0x1bb1d]=std::uint8_t(random32()%segmentCount);
    // Exercise zero gates frequently instead of relying on one in 256.
    ram[0x1bb72]=(random32()&1)?0:0x80;ram[0x1bb46]=(random32()&1)?0:0x80;
    ram[0x63ee0]=(random32()&1)?0:std::uint8_t(random32());
    audio::putLong(ram.data(),0x64aec,(random32()&1)?0x9cedcd02:random32());
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!validMemory(a,1)){badAddress=a;invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!validMemory(a,2)){badAddress=a;invalid=true;return 0;}return audio::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!validMemory(a,4)){badAddress=a;invalid=true;return 0;}return audio::longword(ram.data(),a);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!validWrite(a,1)){badAddress=a;invalid=true;return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){
    if(executing&&mmio(a)){writes.push_back({a,std::uint16_t(v)});return;}
    if(!validWrite(a,2)){badAddress=a;invalid=true;return;}audio::putWord(ram.data(),a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v){
    if(executing&&mmio(a)&&mmio(a+2)){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);return;}
    if(!validWrite(a,4)){badAddress=a;invalid=true;return;}audio::putLong(ram.data(),a,v);
}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: physics_update_oracle payload label chipram [...]");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Unexpected payload size");
    mark(0x1c230,72);mark(0x1bd02,12);mark(0x1bc94,12);mark(0x1bbf6,2);
    for(auto a:motion::outputWords)mark(a,2);
    for(auto a:wheels::speedOutputs)mark(a,2);for(auto a:wheels::dampingOutputs)mark(a,2);
    for(auto a:scr_test::trackSamplingOutputBytes())mark(a,1);
    for(auto r:response::observedRanges)mark(r.begin,r.end-r.begin);
    for(auto r:integration::stateRanges)mark(r.begin,r.end-r.begin);
    for(auto a:effects::stateWords)mark(a,2);for(auto a:effects::stateBytes)mark(a,1);mark(effects::seedAddress,4);
    for(auto a:steering::outputBytes())mark(a,1);
    for(auto a:scr::test::carDynamics::wordOutputs)mark(a,2);mark(scr::test::carDynamics::byteOutput,1);
    for(unsigned i=0;i<4;++i)mark(audio::channels+4*i,2);mark(audio::guard,2);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned validCaptures=0,skipped=0;std::cout<<"{\"seed\":"<<seed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2){
        const std::string label=argv[argument];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid label");
        const auto bytes=readFile(argv[argument+1]);if(bytes.size()!=ram.size())throw std::runtime_error("Expected 512K capture");
        auto immutable=codeRanges;immutable.insert(immutable.end(),tableRanges.begin(),tableRanges.end());
        for(auto range:immutable)if(!std::equal(payload.begin()+range[0]-0xe700,payload.begin()+range[1]-0xe700,bytes.begin()+range[0]))
            throw std::runtime_error("Captured original code/table differs from payload");
        std::copy(bytes.begin(),bytes.end(),captureRam.begin());ram=captureRam;
        if(argument>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<'"';
        const auto data=fx::readData(ram);const auto count=data.track.segmentCount;
        if(!count){++skipped;std::cout<<",\"skipped\":\"No loaded track\"}";continue;}
        if(count>100||ram[0x1bb1c]>=count)throw std::runtime_error("Invalid track metadata");
        const auto start=total;single(data);
        // Every countdown byte on both scheduler paths and four multiplayer modes.
        for(unsigned timer=0;timer<256;++timer)for(unsigned mode=0;mode<8;++mode){
            ram=captureRam;ram[0x1bbdf]=std::uint8_t(timer);ram[0x1bb72]=(mode&1)?0x80:0;
            ram[0x57c3c]=(mode&2)?0x80:0;ram[0x1bbe0]=(mode&4)?0x80:0;
            ram[0x1bbc4]=(mode&4)?0x80:0;ram[0x1bbe1]=std::uint8_t(mode);
            if(mode&4)ram[0x1bb1d]=ram[0x1bb1c]; // Admit the multiplayer side-change gate.
            ram[0x1bb46]=0x80;ram[0x63ee0]=0;
            single(data);
        }
        // Each loaded segment, forward-distance edges, both scheduling paths.
        for(unsigned segment=0;segment<count;++segment)for(unsigned distance:{0u,1u,0x7fffu,0x8000u,0xff00u,0xffffu})for(unsigned gate=0;gate<2;++gate){
            ram=captureRam;ram[0x1bb1c]=std::uint8_t(segment);ram[0x1bb72]=std::uint8_t(gate);
            audio::putWord(ram.data(),0x1bb10,std::uint16_t(distance));single(data);
        }
        for(unsigned i=0;i<8192;++i){ram=captureRam;randomize(count);single(data);}
        // Persistent native state: never reconstructed from emulator outputs.
        // External inputs are identically scripted on both sides at each tick.
        for(unsigned sequence=0;sequence<16;++sequence){
            ram=captureRam;ram[0x1bbdf]=std::uint8_t(sequence*16);nativeRam=ram;auto state=fx::readState(nativeRam);
            for(unsigned tick=0;tick<512;++tick){
                for(auto* r:{&ram,&nativeRam}){
                    (*r)[0x1bb72]=std::uint8_t((tick+sequence)%3!=0);(*r)[0x1bbcd]=(tick&1)?0x80:0;
                    (*r)[0x1bb1c]=std::uint8_t((tick/16+sequence)%count);
                    (*r)[0x1bb0a]=std::uint8_t(tick);audio::putWord(r->data(),0x1bb10,std::uint16_t(tick*128));
                    audio::putWord(r->data(),0x1bd5a,std::uint16_t(tick*127));
                }
                check(data,state);
            }
        }
        ++validCaptures;std::cout<<",\"segment_count\":"<<unsigned(count)<<",\"cases\":"<<total-start<<'}';
    }
    if(!validCaptures)throw std::runtime_error("A loaded-track capture is required");
    if(!coverage[0x61896]||coverage[0x61896]>=total||!audioRequests)throw std::runtime_error("Missing scheduler/audio coverage");
    if(coverage[0x618c0]!=total||coverage[0x5b32e]!=total||coverage[0x63e2e]!=total)
        throw std::runtime_error("A common driver stage was not called exactly once per update");
    std::cout<<"],\"total_cases\":"<<total<<",\"mismatches\":0,\"valid_track_captures\":"<<validCaptures
             <<",\"skipped_menu_captures\":"<<skipped<<",\"conditional_path_cases\":"<<coverage[0x61896]
             <<",\"skipped_path_cases\":"<<total-coverage[0x61896]<<",\"audio_requests\":"<<audioRequests
             <<",\"max_instructions\":"<<maxInstructions<<",\"total_instructions\":"<<totalInstructions
             <<",\"persistent_native_sequence_ticks_per_capture\":8192,\"whole_ram_compared_except_test_infrastructure\":true"
             <<",\"ordered_paula_writes_compared\":true,\"unmodified_original_code\":true,\"registers_reset_between_calls\":false"
             <<",\"registers_reset_between_ticks\":true,\"common_stage_calls_per_tick_verified\":true"
             <<",\"multiplayer_helper_calls\":"<<coverage[0x58758]<<",\"random_helper_calls\":"<<coverage[0x62574]
             <<",\"message_helper_calls\":"<<coverage[0x5e4ec]
             <<",\"complete_original_driver\":true,\"interrupts_interleaved\":false,\"final_prng_state\":"<<rng<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

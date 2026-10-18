#include "audio_playback_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace f=scr::test::audio;
std::array<std::uint8_t,0x80000> ram{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
std::vector<scr::AudioRegisterWrite> writes;
bool executing=false,invalid=false,returned=false;
unsigned instructions=0,channel=0;
std::uint64_t cases=0;
std::uint32_t rng=0x5041554c;
std::uint32_t random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
std::vector<std::uint8_t> readFile(const char* p){
    std::ifstream file(p,std::ios::binary);if(!file)throw std::runtime_error("Cannot read fixture");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool valid(unsigned a,unsigned n){return a<ram.size()&&n<=ram.size()-a&&(n==1||!(a&1));}
bool writable(unsigned a,unsigned n){
    return executing&&valid(a,n)&&((a>=stack-20&&a+n<=stack)
        ||(a>=f::guard&&a+n<=f::guard+2)||(a>=f::channels+channel*4&&a+n<=f::channels+channel*4+2));
}
void hook(unsigned pc){
    if(invalid){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    if(pc<f::entry||pc>=f::end||(pc&1)||++instructions>32){invalid=true;m68k_end_timeslice();}
}
void check(std::uint16_t id,const scr::AudioSampleData& data,const scr::AudioPlaybackState& initial){
    f::writeData(ram.data(),data);f::writeState(ram.data(),initial);channel=data.samples[id&7].channel;
    auto expectedState=initial;const auto expected=scr::requestSample(id,data,expectedState);
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,f::entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();std::array<unsigned,15> regs{};
    for(unsigned i=0;i<15;++i){regs[i]=random32();m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),regs[i]);}
    regs[0]=(regs[0]&0xffff0000u)|id;m68k_set_reg(M68K_REG_D0,regs[0]);
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,f::entry);
    invalid=returned=false;instructions=0;writes.clear();executing=true;m68k_execute(4000);executing=false;
    if(invalid||!returned||instructions!=32||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Original playback execution contract failed");
    for(unsigned i=0;i<15;++i)if(m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_D0+i))!=regs[i])
        throw std::runtime_error("Playback changed preserved register");
    const auto actual=f::readState(ram.data());
    if(actual.completedInterrupts!=expectedState.completedInterrupts||actual.dmaEnableGuard!=expectedState.dmaEnableGuard||writes.size()!=expected.size())
        throw std::runtime_error("Playback state/effect count mismatch");
    for(unsigned i=0;i<expected.size();++i)if(writes[i].address!=expected[i].address||writes[i].value!=expected[i].value)
        throw std::runtime_error("Playback ordered register-write mismatch");
    ++cases;
}
scr::AudioSampleData randomData(){
    scr::AudioSampleData d;
    for(auto& s:d.samples)s={random32(),random32(),std::uint16_t(random32()),std::uint16_t(random32()),std::uint16_t(random32()%4)};
    for(auto& m:d.channelMasks)m=std::uint16_t(random32());return d;
}
scr::AudioPlaybackState randomState(){scr::AudioPlaybackState s;for(auto& c:s.completedInterrupts)c=std::uint16_t(random32());s.dmaEnableGuard=std::uint16_t(random32());return s;}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!valid(a,1)){invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!valid(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!valid(a,4)){invalid=true;return 0;}return f::longword(ram.data(),a);}
void m68k_write_memory_8(unsigned int,unsigned int){invalid=true;}
void m68k_write_memory_16(unsigned int a,unsigned int v){
    if(executing&&a>=0xdff000&&a<0xdff200&&!(a&1)){writes.push_back({a,std::uint16_t(v)});return;}
    if(!writable(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v){
    if(executing&&a>=0xdff000&&a<0xdff1fe&&!(a&1)){writes.push_back({a,std::uint16_t(v>>16)});writes.push_back({a+2,std::uint16_t(v)});return;}
    if(!writable(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);
}
}
int main(int argc,char** argv)try{
    if(argc<2||(argc-2)%2)throw std::runtime_error("Usage: audio_playback_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned captured=0;
    for(int a=2;a<argc;a+=2){
        const auto chip=readFile(argv[a+1]);if(chip.size()!=ram.size())throw std::runtime_error("Capture size");
        if(!std::equal(payload.begin()+f::entry-0xe700,payload.begin()+f::end-0xe700,chip.begin()+f::entry))throw std::runtime_error("Capture code changed");
        std::copy(chip.begin(),chip.end(),ram.begin());const auto d=f::readData(ram.data());const auto s=f::readState(ram.data());
        for(unsigned id=0;id<8;++id){check(std::uint16_t(id),d,s);++captured;}
    }
    auto d=randomData();auto s=randomState();
    for(unsigned id=0;id<65536;++id)check(std::uint16_t(id),d,s);
    for(unsigned mask=0;mask<65536;++mask){d.channelMasks.fill(std::uint16_t(mask));check(std::uint16_t(mask),d,s);}
    for(unsigned i=0;i<10000;++i)check(std::uint16_t(random32()),randomData(),randomState());
    unsigned rejected=0;for(unsigned channelId:{4u,255u,65535u}){d.samples[0].channel=std::uint16_t(channelId);try{(void)scr::requestSample(0,d,s);}catch(const std::out_of_range&){++rejected;}}
    if(rejected!=3)throw std::runtime_error("Invalid channel accepted");
    std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"captured_cases\":"<<captured
        <<",\"register_writes_per_case\":10,\"instructions_per_case\":32,\"invalid_channels_rejected\":3,\"audio_waveform_verified\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

// Original scheduler and all local callees execute, including the original HUD
// renderers. Only their pixels/text formatting are excluded from native output.
#include "race_timing_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace f=scr::test::timing;
using Ram=std::array<std::uint8_t,0x80000>;
Ram ram{},nativeRam{},baseline{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
std::array<bool,0x80000> gameplay{};
std::uint32_t rng=0x54494d45,registerRng=0x36384354;
std::uint64_t cases=0,graphicsCalls=0;
bool executing=false,invalid=false,returned=false;
unsigned pcNow=0,instructions=0,maximumInstructions=0,framebufferBase=0,renderBase=0;
scr::RaceTimingEffects originalEffects{};
std::uint32_t next(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint8_t byte(){return static_cast<std::uint8_t>(next(rng));}
bool inside(unsigned a,unsigned n,unsigned b,unsigned e){return a>=b&&a<e&&n<=e-a;}
std::vector<std::uint8_t> readFile(const char* name){std::ifstream in(name,std::ios::binary);if(!in)throw std::runtime_error(std::string("Cannot read ")+name);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void bad(){invalid=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();}
void event(scr::RaceTimingEventKind kind,std::uint8_t value){
    if(originalEffects.count>=originalEffects.events.size()){bad();return;}
    originalEffects.events[originalEffects.count++]={kind,value};
}
void hook(unsigned pc){
    pcNow=pc;if(invalid){bad();return;}if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    if(pc==0x5ded0)event(scr::RaceTimingEventKind::lapTimerHud,ram[0x1bbcc]?0x80:0);
    if(pc==0x5dca6)event(scr::RaceTimingEventKind::resultsHud,ram[0x1bbb2]?11:7);
    if(pc==0x5dd80)event(scr::RaceTimingEventKind::lapCounterHud,ram[0x1bb20]);
    if(pc==0x5e0d6)event(scr::RaceTimingEventKind::leaderIcon,std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D2)));
    if(pc==0x594c6||pc==0x69f42)++graphicsCalls;
    bool allowed=false;for(auto r:f::codeRanges)allowed|=inside(pc,2,r.begin,r.end);
    if(!allowed||(pc&1)||++instructions>100000)bad();
}
bool validRead(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
bool graphicsOutput(unsigned a,unsigned n){
    return inside(a,n,framebufferBase,framebufferBase+64000)||inside(a,n,renderBase,renderBase+32000);
}
bool textOutput(unsigned a,unsigned n){return n==1&&(a==0x1bb6d||a==0x1bb6e||a==0x1bbd3||inside(a,1,0x5983e,0x59845));}
bool allowedWrite(unsigned a,unsigned n){
    if(!executing||!validRead(a,n))return false;
    if(inside(a,n,stack-256,stack))return true;
    if(n==1&&gameplay[a])return true;
    if(textOutput(a,n))return true;
    if(n==4&&a==0x6a58c)return true;
    return n==2&&graphicsOutput(a,n)&&(inside(pcNow,2,0x594c6,0x59806)||inside(pcNow,2,0x69f42,0x69fe8));
}
void original(unsigned entry,unsigned player=0,std::uint8_t delta=0){
    executing=false;
    if(ram[0x57c3c]||ram[0x5924a])throw std::runtime_error("Local mode and primary font required");
    framebufferBase=f::longword(ram.data(),0x6a584);renderBase=f::longword(ram.data(),0x6a58c);
    if(framebufferBase<0x68000||!inside(framebufferBase,64000,0,0x80000)||renderBase<0x68000||!inside(renderBase,32000,0,0x80000))throw std::runtime_error("Invalid framebuffer mapping");
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(registerRng));
    m68k_set_reg(M68K_REG_D0,(next(registerRng)&0xffffff00u)|delta);
    m68k_set_reg(M68K_REG_D1,(next(registerRng)&0xffff0000u)|player);
    m68k_set_reg(M68K_REG_D2,next(registerRng)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(registerRng)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;instructions=0;originalEffects={};executing=true;m68k_execute(4000000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)throw std::runtime_error("Original timing execution/access failed at PC "+std::to_string(pcNow));
    maximumInstructions=std::max(maximumInstructions,instructions);
}
void check(const scr::RaceTimingInputs& input,unsigned entry=f::entry,unsigned player=0,std::uint8_t delta=0,scr::RaceTimingState* persistent=nullptr){
    f::writeInputs(ram.data(),input);nativeRam=ram;
    auto local=f::readState(ram.data());auto& state=persistent?*persistent:local;scr::RaceTimingEffects effects{};
    if(entry==f::entry)effects=scr::updateRaceTiming(input,state);
    else if(entry==0x5df32)effects=scr::incrementLapClock(delta,player,state);
    else if(entry==0x5dbd6)scr::updateBlinkCountdown(input,state);
    else throw std::runtime_error("Unsupported oracle entry");
    f::writeState(nativeRam.data(),state);original(entry,player,delta);
    std::array<f::Range,8> exclusions{{{0,8},{stack-256,stack+4},{sentinel,sentinel+4},
        {framebufferBase,framebufferBase+64000},{renderBase,renderBase+32000},
        {0x1bb6d,0x1bb6f},{0x1bbd3,0x1bbd4},{0x5983e,0x59845}}};
    std::sort(exclusions.begin(),exclusions.end(),[](auto a,auto b){return a.begin<b.begin;});
    const auto compare=[&](unsigned begin,unsigned end){
        if(begin>=end||!std::memcmp(ram.data()+begin,nativeRam.data()+begin,end-begin))return;
        for(unsigned a=begin;a<end;++a)if(ram[a]!=nativeRam[a]){std::cerr<<"Timing mismatch case="<<cases<<" address=0x"<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';throw std::runtime_error("Timing state mismatch");}
    };
    unsigned cursor=0;for(auto range:exclusions){compare(cursor,range.begin);cursor=std::max(cursor,range.end);}compare(cursor,unsigned(ram.size()));
    if(effects.count!=originalEffects.count)throw std::runtime_error("Timing event count mismatch");
    for(unsigned n=0;n<effects.count;++n)if(effects.events[n].kind!=originalEffects.events[n].kind||effects.events[n].value!=originalEffects.events[n].value)throw std::runtime_error("Timing event order/value mismatch");
    ++cases;
}
void reset(){ram=baseline;}
scr::RaceTimingInputs baseInput(){
    auto in=f::readInputs(baseline.data());in.selectedTrackInDivision=0;in.offTrackStateFlags=0;in.segmentIndex={2,3};
    in.lapLineSegment=1;in.finishLineSegment=0;in.numberOfTrackSegments=44;in.maximumLaps=4;in.currentPlayerId=11;
    in.raceStartTimer=0;in.wheelMovementActive=0x80;in.raceCompletionCheckFlag=0;return in;
}
void randomState(){for(unsigned a=0;a<ram.size();++a)if(gameplay[a])ram[a]=byte();}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!validRead(a,1)){invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!validRead(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!allowedWrite(a,1)){std::cerr<<"Write8 0x"<<std::hex<<a<<" pc0x"<<pcNow<<std::dec<<'\n';invalid=true;return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!allowedWrite(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!allowedWrite(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: race_timing_oracle payload.bin label chipram.bin [...]");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Invalid payload size");
    for(auto a:f::byteOutputs)gameplay[a]=true;for(auto r:f::arrayOutputs)for(unsigned a=r.begin;a<r.end;++a)gameplay[a]=true;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":"<<rng<<",\"captures\":[";
    for(int a=2;a<argc;a+=2){
        const std::string label=argv[a];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid label");
        const auto capture=readFile(argv[a+1]);if(capture.size()!=ram.size())throw std::runtime_error("Expected512KiB capture");
        for(auto r:f::codeRanges)if(!std::equal(payload.begin()+r.begin-0xe700,payload.begin()+r.end-0xe700,capture.begin()+r.begin))throw std::runtime_error("Captured code differs");
        std::copy(capture.begin(),capture.end(),ram.begin());baseline=ram;check(f::readInputs(ram.data()));
        if(a>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\"}";
    }
    const auto captureCases=cases;
    // Complete byte-pair sweeps of the clock's initial BCD addition, and every
    // seconds/minutes encoding when a carry reaches that component.
    for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b){
        reset();auto in=baseInput();auto s=f::readState(ram.data());s.lapCounter[0]=0;s.times[0]={0,0,std::uint8_t(a)};f::writeState(ram.data(),s);check(in,0x5df32,0,std::uint8_t(b));
        reset();s=f::readState(ram.data());s.lapCounter[0]=0;s.times[0]={std::uint8_t(a),std::uint8_t(b),0x99};f::writeState(ram.data(),s);check(in,0x5df32,0,1);
    }
    const auto bcdCases=cases-captureCases;
    for(unsigned timer=0;timer<256;++timer)for(unsigned flags=0;flags<8;++flags){
        reset();auto in=baseInput();in.raceStartTimer=flags&1?1:0;in.wheelMovementActive=flags&2?0x80:0;
        ram[0x1bb6c]=std::uint8_t(timer);ram[0x1bbcd]=flags&4?0xff:0;check(in,0x5dbd6);
    }
    for(unsigned a=0;a<256;++a){reset();auto in=baseInput();ram[0x1bbcf]=std::uint8_t(a);ram[0x1bbcc]=std::uint8_t(a);check(in);}
    for(unsigned player=0;player<2;++player)for(unsigned lap=0;lap<6;++lap)for(unsigned phase=0;phase<4;++phase)for(unsigned id=0;id<12;++id){
        reset();auto in=baseInput();in.currentPlayerId=std::uint8_t(id);in.selectedTrackInDivision=0x80;in.segmentIndex[player]=phase&1?1:0;
        in.offTrackStateFlags=phase&2?0x40:0;in.trackProgress[player]=std::uint16_t((lap*43)&255);
        auto s=f::readState(ram.data());s.crossingArmed[player]=phase&2?0x80:0;s.lapCounter[player]=std::uint8_t(lap);s.raceComplete=0;
        s.times[player]={0,0x12,0x34};for(unsigned n=2;n<24;++n)s.times[n]={9,0x59,0x99};f::writeState(ram.data(),s);check(in);
    }
    const auto boundaryCases=cases-captureCases-bcdCases;
    for(unsigned n=0;n<6000;++n){
        reset();randomState();auto in=baseInput();in.currentPlayerId=byte()%12;in.selectedTrackInDivision=byte();in.offTrackStateFlags=byte();
        in.trackProgress={std::uint16_t(next(rng)),std::uint16_t(next(rng))};in.raceCompletionCheckFlag=byte();in.raceStartTimer=byte();in.wheelMovementActive=byte();
        in.playerDistanceDifference=std::uint16_t(next(rng));in.maximumLaps=byte();in.numberOfTrackSegments=byte();
        if(n%2==0){in.segmentIndex[n%3?0:1]=in.lapLineSegment;ram[0x1bb1e]=ram[0x1bb1f]=0;ram[0x1bb20]=byte()%6;ram[0x1bb21]=byte()%6;in.trackProgress={std::uint16_t(byte()),std::uint16_t(byte())};}
        if(n%3==0){ram[0x1bbaf]=0;in.maximumLaps=ram[0x1bb20];}
        check(in);
    }
    const auto randomCases=cases-captureCases-bcdCases-boundaryCases;
    reset();auto in=baseInput();auto state=f::readState(ram.data());state.lapCounter={0,0};state.crossingArmed={0,0};state.raceComplete=0;f::writeState(ram.data(),state);
    for(unsigned n=0;n<4096;++n){in.segmentIndex[0]=std::uint8_t((n/32)%44);in.segmentIndex[1]=std::uint8_t((n/37)%44);in.selectedTrackInDivision=n&512?0x80:0;check(in,f::entry,0,0,&state);}
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"bcd_cases\":"<<bcdCases<<",\"boundary_cases\":"<<boundaryCases
             <<",\"random_cases\":"<<randomCases<<",\"independent_native_sequence_cases\":4096,\"total_cases\":"<<cases
             <<",\"mismatches\":0,\"original_graphics_calls\":"<<graphicsCalls<<",\"maximum_instructions\":"<<maximumInstructions
             <<",\"original_code_unmodified\":true,\"graphics_and_text_outputs_excluded\":true,\"network_supported\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<'\n'<<e.what()<<'\n';return 1;}

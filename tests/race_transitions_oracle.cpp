// Differential execution of unmodified original control instructions. Only
// presentation, platform audio and waiting callees are explicit boundaries.
#include "race_transitions_fixture.h"
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
namespace f=scr::test::transitions;
using Ram=std::array<std::uint8_t,0x80000>;
using Action=scr::RaceTransitionAction;using Event=scr::RaceTransitionEventKind;
Ram ram{},nativeRam{},baseline{};std::array<bool,0x80000> writable{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
std::uint32_t rng=0x52414345;std::uint64_t cases=0,originalInstructions=0;
unsigned pcNow=0,mode=0,instructions=0,resumePc=0,stubReturnPc=0;bool executing=false,invalid=false,returned=false,paused=false,yieldedStub=false;
scr::RaceTransitionEffects originalEffects{};
std::uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
std::uint8_t byte(){return static_cast<std::uint8_t>(next());}
bool inside(unsigned a,unsigned n,unsigned b,unsigned e){return a>=b&&a<e&&n<=e-a;}
std::vector<std::uint8_t> readFile(const char* name){std::ifstream in(name,std::ios::binary);if(!in)throw std::runtime_error(std::string("Cannot read ")+name);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void stop(Action a=Action::continueTick){originalEffects.action=a;returned=true;if(a==Action::pauseWait)resumePc=yieldedStub?stubReturnPc:m68k_get_reg(nullptr,M68K_REG_PC);m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();}
void event(Event k,std::uint8_t value=0){if(originalEffects.count>=originalEffects.events.size())throw std::runtime_error("Original event overflow");originalEffects.events[originalEffects.count++]={k,value};}
void skip(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);stubReturnPc=f::longword(ram.data(),sp);m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,sentinel+8);yieldedStub=true;m68k_end_timeslice();}
void hook(unsigned pc){
    pcNow=pc;++instructions;++originalInstructions;
    if(invalid||instructions>2000){invalid=true;stop();return;}
    if(pc==sentinel){stop(mode==5?Action::raceEnd:Action::continueTick);return;}
    if(mode==0&&(pc==0x5d574||pc==0x5d5a0)){stop(pc==0x5d574?Action::raceEnd:Action::continueTick);return;}
    if(mode==2&&pc==0x5d674){stop(Action::raceEnd);return;}
    if(mode==3&&(pc==0x5d47e||pc==0x5d402||pc==0x5d674)){
        if(pc==0x5d402)originalEffects.restartSegment=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1));
        stop(pc==0x5d402?Action::restartRace:pc==0x5d674?Action::quit:Action::continueTick);return;
    }
    if(pc==0x5e0aa)event(Event::renderPlayerIcon,std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D2)));
    if(pc==0x5e0ec){skip();return;}
    if(pc==0x5fae8){event(Event::renderMessagePanel);skip();return;}
    if(pc==0x5e502)event(Event::displayEndMessage,std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D2)));
    if(pc==0x5e508){skip();return;}
    if(pc==0x6091a){event(Event::renderExistingHole,std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D2)));skip();return;}
    if(pc==0xf3f0){event(Event::disableAudio);skip();return;}
    if(pc==0x57964)event(Event::updateNetworkFlags);
    if(pc==0x6488a){event(Event::waitForFire);skip();return;}
    if(pc==0x57b86){event(Event::waitForUnpause);skip();paused=true;stop(Action::pauseWait);return;}
    if(pc==0xf362){event(Event::resumeEngine,std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D0)));skip();return;}
    if(pc==0x5b56c){skip();return;}
    if(pc==0x6444e){event(Event::fadeToMenu);originalEffects.fadeColor=std::uint16_t(m68k_get_reg(nullptr,M68K_REG_D0));skip();return;}
    bool allowed=inside(pc,2,0x5e0aa,0x5e0c4);
    for(auto r:f::codeRanges)allowed|=inside(pc,2,r.begin,r.end);
    if(!allowed||(pc&1)){invalid=true;stop();}
}
bool validRead(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
bool allowedWrite(unsigned a,unsigned n){
    if(!executing||!validRead(a,n))return false;
    if(inside(a,n,stack-256,stack))return true;
    for(unsigned k=0;k<n;++k)if(!writable[a+k])return false;
    return true;
}
void execute(unsigned selectedMode,bool resume=false){
    mode=selectedMode;returned=invalid=false;instructions=0;originalEffects={};executing=false;
    if(!resume){
        constexpr unsigned entries[]{0x5d4ba,0x5dac0,0x5d57a,0x5d5ac,0x5d9dc,0x5d674};
        const auto entry=entries[mode];f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);
        f::putWord(ram.data(),sentinel,0x4e71);f::putWord(ram.data(),sentinel+2,0x4e71);f::putWord(ram.data(),sentinel+8,0x4e71);m68k_pulse_reset();
        for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next());
        m68k_set_reg(M68K_REG_D1,next()&0xffff00ffu);
        m68k_set_reg(M68K_REG_SR,0x2700|(next()&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    }else m68k_set_reg(M68K_REG_PC,resumePc);
    executing=true;
    do{yieldedStub=false;m68k_execute(100000);if(yieldedStub&&!returned)m68k_set_reg(M68K_REG_PC,stubReturnPc);}while(yieldedStub&&!returned);
    executing=false;
    if(!returned||invalid)throw std::runtime_error("Original transition execution failed at PC "+std::to_string(pcNow)+" case "+std::to_string(cases)+" mode "+std::to_string(mode)+" frames "+std::to_string(ram[0x616d8]));
}
void compare(const scr::RaceTransitionEffects& e){
    if(e.action!=originalEffects.action||e.count!=originalEffects.count||e.restartSegment!=originalEffects.restartSegment||e.fadeColor!=originalEffects.fadeColor)
        throw std::runtime_error("Transition action/event count mismatch at case "+std::to_string(cases));
    for(unsigned n=0;n<e.count;++n)if(e.events[n].kind!=originalEffects.events[n].kind||e.events[n].value!=originalEffects.events[n].value)
        throw std::runtime_error("Transition ordered event mismatch at case "+std::to_string(cases));
    const auto compareRange=[&](unsigned begin,unsigned end){
        if(!std::memcmp(ram.data()+begin,nativeRam.data()+begin,end-begin))return;
        for(unsigned a=begin;a<end;++a)if(ram[a]!=nativeRam[a]){
            std::cerr<<"Transition mismatch case="<<cases<<" address=0x"<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
            throw std::runtime_error("Transition state mismatch");
        }
    };
    compareRange(8,sentinel);compareRange(sentinel+10,stack-256);compareRange(stack+4,unsigned(ram.size()));
    ++cases;
}
void check(unsigned selectedMode,const scr::RaceTransitionInputs& in){
    f::writeInputs(ram.data(),in);nativeRam=ram;auto s=f::readState(ram.data());scr::RaceTransitionEffects e{};
    if(selectedMode==0)e=scr::inspectRaceEnd(in,s);
    else if(selectedMode==1){if(!scr::swapRaceBuffers(s))throw std::runtime_error("Unexpected native frame wait");}
    else if(selectedMode==2)e=scr::finishRaceEnd(s);
    else if(selectedMode==3)e=scr::continueRaceTransition(in,s);
    else if(selectedMode==4)e=scr::beginRacePause(in,s);
    else if(selectedMode==5)e=scr::finishLocalRaceExit(in,s);
    f::writeState(nativeRam.data(),s);paused=false;execute(selectedMode);compare(e);
    if(paused){nativeRam=ram;e=scr::resumeRacePause(s);f::writeState(nativeRam.data(),s);execute(4,true);compare(e);}
}
void reset(){ram=baseline;ram[0x57c3c]=0;}
void randomState(){for(unsigned a=0;a<ram.size();++a)if(writable[a])ram[a]=byte();}
scr::RaceTransitionInputs randomInput(){return {byte(),byte(),byte(),byte(),byte(),byte(),byte(),byte(),byte(),bool(byte()&1),bool(byte()&1),
    std::uint8_t(byte()&0x7f),byte(),byte(),std::uint16_t(next()),next()};}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!validRead(a,1)){invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!validRead(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!allowedWrite(a,1)){invalid=true;return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!allowedWrite(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!allowedWrite(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);}
}
int main(int argc,char** argv)try{
    if(argc!=3&&argc!=4)throw std::runtime_error("Usage: race_transitions_oracle payload.bin [label] chipram.bin");
    const auto payload=readFile(argv[1]),capture=readFile(argv[argc-1]);
    if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Invalid payload/capture size");
    for(auto r:f::codeRanges)if(!std::equal(payload.begin()+r.begin-0xe700,payload.begin()+r.end-0xe700,capture.begin()+r.begin))throw std::runtime_error("Captured code differs");
    std::copy(capture.begin(),capture.end(),baseline.begin());
#define MARK_BYTE(field,address) writable[address]=true;
    SCR_TRANSITION_BYTES(MARK_BYTE)
#undef MARK_BYTE
#define MARK_WORD(field,address) writable[address]=writable[address+1]=true;
    SCR_TRANSITION_WORDS(MARK_WORD)
#undef MARK_WORD
#define MARK_LONG(field,address) for(unsigned a=address;a<address+4;++a)writable[a]=true;
    SCR_TRANSITION_LONGS(MARK_LONG)
#undef MARK_LONG
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned n=0;n<256;++n)for(unsigned flags=0;flags<16;++flags){
        reset();auto in=randomInput();in.raceStartTimer=flags&1?1:0;in.offTrackFlags=flags&2?0x80:0;
        in.wheelMovementActive=flags&4?0x80:0;in.quitPressed=flags&8;in.carHeightLow=std::uint8_t(n);
        ram[0x1bb41]=std::uint8_t(n);ram[0x1bb6c]=n&1?0:0x80;check(3,in);
        reset();in.lapDisplayDuration=flags&1?1:0;in.raceStartTimer=flags&2?1:0;
        in.wheelMovementActive=flags&4?0x80:0;in.raceComplete=flags&8?1:0;
        ram[0x1bb6f]=std::uint8_t(n);ram[0x1bbc8]=std::uint8_t(n);check(0,in);
        reset();ram[0x616d8]=0;ram[0x1bbc8]=std::uint8_t(n);ram[0x5dabe]=std::uint8_t(flags);check(1,in);
    }
    const auto boundaryCases=cases;
    for(unsigned n=0;n<12000;++n){
        reset();randomState();auto in=randomInput();
        if(n%2==0){in.raceStartTimer=0;in.offTrackFlags|=0x80;ram[0x1bb6c]=n%3?0:byte();}
        if(n%3==0){in.lapDisplayDuration=0;ram[0x1bb6f]=ram[0x1bbc8]=0x80;in.raceComplete=0;}
        if(n%5==0){in.majorImpactCooldown=0x45;in.holePosition=byte();}
        const unsigned selectedMode=n%6;if(selectedMode==1)ram[0x616d8]=0;check(selectedMode,in);
    }
    for(unsigned n=0;n<256;++n)for(unsigned key=0;key<2;++key){reset();ram[0x1bba4]=std::uint8_t(n);auto in=randomInput();in.pausePressed=key;check(4,in);}
    // Every local selector and random tail byte. Single-player season exits
    // skip restoring the saved Practice hole position. Original saveRandomState
    // executes both writes, exposing the overlap in the saved longword.
    for(unsigned track=0;track<256;++track)for(unsigned tail=0;tail<256;++tail){
        reset();auto in=randomInput();in.selectedTrackInDivision=std::uint8_t(track);in.randomTail=std::uint8_t(tail);
        in.randomSeed=0x12ab3456;in.savedHolePosition=std::uint8_t(tail^track);check(5,in);
    }
    // The original busy wait is deliberately not spun. Its state is unchanged
    // until a platform frame event reaches zero; verify that native contract.
    for(unsigned n=1;n<256;++n){reset();auto s=f::readState(ram.data());s.framesToWait=std::uint8_t(n);nativeRam=ram;f::writeState(nativeRam.data(),s);const auto before=nativeRam;
        if(scr::swapRaceBuffers(s))throw std::runtime_error("Native failed to expose frame wait");f::writeState(nativeRam.data(),s);if(nativeRam!=before)throw std::runtime_error("Frame wait mutated state");}
    std::cout<<"{\"total_cases\":"<<cases<<",\"boundary_cases\":"<<boundaryCases<<",\"mismatches\":0,\"original_instructions\":"<<originalInstructions
             <<",\"original_code_unmodified\":true,\"ordered_boundary_events_compared\":true,\"whole_nonpresentation_ram_compared\":true,\"frame_wait_values\":255,\"network_supported\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "opponent_driving_fixture.h"
#include "draw_bridge_fixture.h"
#include "assets/native_startup.h"
#include "race/native_race_initialization.h"
#include "track/track_initialization.h"
#include "m68k.h"
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace f=scr::test::opponent;
f::Ram original{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},maximum{};std::uint64_t calls{},compared{},totalInstructions{};bool failed{},returned{};std::string failure;
unsigned currentTrack{},currentSegment{},currentFraction{},currentTick{};
std::array<unsigned,8> segmentWraps{},airborne{},actionTicks{};
std::array<std::set<unsigned>,3> observedWrites{};unsigned writeStage{};
std::uint32_t seed=0x4f50504f;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
void fail(const char* why,unsigned a){failed=true;if(failure.empty()){std::ostringstream s;s<<why<<"0x"<<std::hex<<a<<" PC0x"<<pc;failure=s.str();}}
void hook(unsigned a){pc=a;if(a==sentinel){returned=true;m68k_end_timeslice();return;}if(a<0xe700||a>=0x7abda||++instructions>20000){fail("Instruction ",a);m68k_end_timeslice();}}
unsigned read(unsigned a){if(a<original.size())return original[a];if(a>=stackBase&&a<stackTop)return stack[a-stackBase];if(a>=sentinel&&a<sentinel+4)return a&1?0x71:0x4e;fail("Read ",a);return 0;}
void write(unsigned a,unsigned v){if(a<original.size()){original[a]=std::uint8_t(v);if(writeStage<3)observedWrites[writeStage].insert(a);}else if(a>=stackBase&&a<stackTop)stack[a-stackBase]=std::uint8_t(v);else fail("Write ",a);}
void cpu(unsigned entry){writeStage=entry==0x63996?0:entry==0x5a186?1:entry==0x63a58?2:3;instructions=0;failed=returned=false;failure.clear();m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));m68k_set_reg(M68K_REG_PC,entry);m68k_execute(500000);if(failed||!returned)throw std::runtime_error("Original failed "+failure);maximum=std::max(maximum,instructions);totalInstructions+=instructions;}
void ranges(const std::set<unsigned>& values){std::cout<<'[';bool first=true;for(auto i=values.begin();i!=values.end();){const auto begin=*i;auto end=begin+1;while(++i!=values.end()&&*i==end)++end;std::cout<<(first?"":",")<<'['<<begin<<','<<end<<']';first=false;}std::cout<<']';}
void compare(const char* stage){for(unsigned a=0;a<original.size();++a){++compared;if(original[a]!=native[a]){std::ofstream("analysis/opponent-driving/original-mismatch.bin",std::ios::binary).write(reinterpret_cast<const char*>(original.data()),original.size());std::ofstream("analysis/opponent-driving/native-mismatch.bin",std::ios::binary).write(reinterpret_cast<const char*>(native.data()),native.size());std::ostringstream s;s<<stage<<" mismatch call"<<calls<<" track"<<currentTrack<<" segment"<<currentSegment<<" fraction"<<currentFraction<<" tick"<<currentTick<<" address0x"<<std::hex<<a<<" original0x"<<unsigned(original[a])<<" native0x"<<unsigned(native[a]);throw std::runtime_error(s.str());}}++calls;}
void logic(){auto d=f::readData(native);f::logic(native,d);cpu(0x63996);compare("logic");}
void geometry(){auto d=f::readData(native);f::geometry(native,d);cpu(0x5a186);compare("geometry");}
void initialize(){auto d=f::readData(native);f::initializeCoordinates(native,d);cpu(0x63a58);compare("initialize");}
void bridge(){(void)scr::test::drawBridge::animate(native);cpu(0x5a794);compare("bridge");}
void put(unsigned a,unsigned v){original[a]=native[a]=std::uint8_t(v);}
void putWord(unsigned a,unsigned v){f::writeWord(original,a,std::uint16_t(v));f::writeWord(native,a,std::uint16_t(v));}
}
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("Usage: opponent_driving_oracle asset-pack");m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned track=0;track<8;++track){currentTrack=track;auto game=scr::runtime::startNativeGame(argv[1]);native=*game.memory;scr::initializeNativeTrack(native,track);scr::runtime::initializeNativePlayerConfiguration(native);put(0x1ca29,track);put(0x1bb1d,native[0x1ca1b]);putWord(0x1bb0c,0x100);putWord(0x1bbec,0x80);original=native;initialize();
        for(unsigned segment=0;segment<native[0x1ca1a];++segment)for(unsigned fraction:{0U,1U,63U,64U,127U,128U,192U,255U})for(unsigned angle=0;angle<4;++angle){
            currentSegment=segment;currentFraction=fraction;put(0x1bb1d,segment);putWord(0x1bb0c,fraction);putWord(0x1bc30,angle<<14);put(0x1bb04,native[0x1c588+segment]&15);put(0x1bb06,native[0x1c588+segment]>>4);geometry();
        }
        put(0x1bbc4,0x80);put(0x1bb74,0);put(0x1bb1d,0);putWord(0x1bb0c,0x100);putWord(0x1bc30,0);put(0x1bb04,0);put(0x1bb06,0);initialize();
        for(unsigned tick=0;tick<2048;++tick){currentTick=tick;const auto before=native[0x1bb1d];put(0x1bbcd,(tick&1)?0x80:0);logic();if(native[0x1bb1d]<before)++segmentWraps[track];if(!native[0x1bb44])++airborne[track];if(native[0x1bbc0])++actionTicks[track];geometry();if(track==5)bridge();}
        // Gate paths retain every byte, including RNG.
        put(0x1bbc4,0);logic();put(0x1bbc4,0x80);put(0x1bb74,1);logic();
        // Independent scalar edge cases. No expected output or evolving
        // original state is fed back to the native implementation.
        for(unsigned sample=0;sample<2048;++sample){
            auto state=f::readState(native);state.randomSeed=next();state.randomTail=std::uint8_t(next());state.segment=std::uint8_t(next()%native[0x1ca1a]);
            state.subSegment=std::uint16_t(next());state.speed=std::uint16_t(next());state.speedFraction=std::uint8_t(next());state.ahead=std::uint8_t(next());state.drafting=std::uint8_t(next());
            state.actionTimer=std::uint8_t(next());state.patternOffset=std::uint8_t(next());state.patternControl=std::uint8_t(next());state.boundaryCollision=std::uint8_t(next());
            for(unsigned i=0;i<3;++i){state.wheelPosition[i]=std::uint16_t(next());state.previousHeight[i]=std::uint16_t(next());state.velocity[i]=std::uint16_t(next());state.bounds[i]=std::uint16_t(next());}
            f::writeState(native,state);original=native;put(0x1bbc4,0x80);put(0x1bb74,0);put(0x1ca29,next()%32);put(0x57c3c,sample&1);put(0x1bbcd,(sample&2)?0x80:0);put(0x1bafc,next());put(0x1bafd,next());logic();
        }
    }
    std::cout<<"{\"calls\":"<<calls<<",\"compared_bytes\":"<<compared<<",\"original_instructions\":"<<totalInstructions<<",\"maximum_instructions\":"<<maximum<<",\"segment_wraps\":[";
    for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<segmentWraps[i];std::cout<<"],\"airborne_ticks\":[";for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<airborne[i];std::cout<<"],\"action_ticks\":[";for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<actionTicks[i];
    std::cout<<"],\"logic_write_ranges\":";ranges(observedWrites[0]);std::cout<<",\"geometry_write_ranges\":";ranges(observedWrites[1]);std::cout<<",\"initialization_write_ranges\":";ranges(observedWrites[2]);
    std::cout<<",\"mismatches\":0,\"original_code_unmodified\":true,\"native_state_reseeded_with_original_outputs\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" track"<<currentTrack<<" segment"<<currentSegment<<" fraction"<<currentFraction<<" tick"<<currentTick<<" calls"<<calls<<'\n';return 1;}

// Actual original main-loop replay with independently evolving native cockpit.
// Original drawing calls and the PAL wheel interrupt prefix execute unchanged.
#include "replay_scene.h"
#include "driving_replay_io.h"
#include "wheel_animation_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace replay=scr::test::replay;namespace wheel=scr::test::wheelAnimation;
using Ram=scr_test::ChipRam;
Ram ram{};std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,entry=0x5d47e;
std::vector<std::uint8_t> savedCpu,boundaryCpu;
unsigned pc{},badAddress{},instructions{},joy{},cia=255,ddra=255,currentTick{},currentTrack{},leafEntry{};
bool invalid{},done{},firstEntry{},outerRunning{},restarting{},leaf{},atBoundary{},atWait{},boundarySeen{};
std::uint64_t ticks{},originalInstructions{},hudPixels{},wheelStates{},spritePlacements{},prefixCalls{},restartUpdates{},postOuterHudChanges{};
std::uint64_t timerCalls{},resultCalls{},fuelCalls{},distanceCalls{},lapCounterCalls{},speedCalls{};
std::uint64_t startupHudSeeds{},startupHudPixels{};
struct EffectCoverage {
    std::uint64_t frames{},messageSnapshots{},visibleMessages{},dropStart{},pressFire{},particleDraws{},dustDraws{},sparkDraws{},restartFrames{};
};
EffectCoverage effectCoverage;
struct OriginalFrameEffects {
    scr::DrivingParticleDraws particles;
    scr::viewer::NativeRaceMessageFrame message;
    std::uint8_t frameCounter{};
    bool rendered{},messageCaptured{},messageGate{},messageVisible{};
};
OriginalFrameEffects originalEffects;
std::string failure;
const scr::viewer::ReplaySceneFrame* frame{};
std::array<std::uint8_t,320*32> boundaryHud{};
struct Scenario {std::string name;std::filesystem::path seed,controls;unsigned maximum{};bool preserveHud{};};
std::string currentScenario;
unsigned read(unsigned address);
unsigned readWord(unsigned address){return (read(address)<<8)|read(address+1);}
void captureOriginalMessage(){
    auto& message=originalEffects.message;
    // Read the independently evolving original inputs, rather than using the
    // native capture helper whose RAM projection this comparison verifies.
    message.active=std::uint8_t(read(0x1bb8e));message.countdown=std::uint8_t(read(0x1bb6c));
    message.pause=std::uint8_t(read(0x1bba4));message.blink=std::uint8_t(read(0x1bb88));
    message.index=std::uint8_t(read(0x5e65b));message.mode=std::uint8_t(read(0x5e65a));
    constexpr unsigned parameters=0x6a16c+51*16;
    message.panel.x=std::uint16_t((readWord(parameters+8)&255)*16);
    message.panel.y=std::uint16_t(readWord(parameters+10)&255);
    message.panel.widthWords=std::uint16_t(readWord(parameters+4)+1);
    message.panel.height=std::uint16_t(readWord(parameters+6)+1);
    originalEffects.messageCaptured=originalEffects.messageGate=true;
    originalEffects.messageVisible=false;
}
void captureOriginalParticle(bool dust){
    const auto offset=m68k_get_reg(nullptr,M68K_REG_D1)&65535;
    if(offset>62||(offset&1))throw std::runtime_error("Original particle index outside admitted 32-particle array");
    const auto base=m68k_get_reg(nullptr,M68K_REG_A4);
    const auto x=readWord(base+offset),y=readWord(base+offset+0x40);
    // 0x60ecc precedes the extra edge-pixel x<254 admission check.
    if(!dust&&x>=254)return;
    originalEffects.particles.push_back({std::uint16_t(x),std::uint16_t(y),std::uint8_t(offset/2),dust});
}
void stop(bool boundary=false){
    done=true;atBoundary=boundary;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();
}
void hook(unsigned address){pc=address;if(invalid){m68k_end_timeslice();return;}try{
    if(leaf&&(address==stopPc||(leafEntry==wheel::vblankEntry&&address==wheel::vblankEnd))){stop();return;}
    if(!leaf){
        // A restart performs two more rendered warm-ups in this same tick.
        // Only its last displayed frame survives, matching the native snapshot.
        if(address==0x64e4c){originalEffects={};originalEffects.rendered=true;originalEffects.frameCounter=ram[0x1bbac];}
        if(address==0x60f5a)captureOriginalParticle(true);
        if(address==0x60ecc)captureOriginalParticle(false);
        if(address==0x651b0)captureOriginalMessage();
        if(address==0x5e540&&originalEffects.messageGate)originalEffects.messageVisible=true;
        if(address==0x651b6)originalEffects.messageGate=false;
        if(address==0x5dac0&&ram[0x616d8]){
            m68k_get_context(boundaryCpu.data());stop();atWait=true;return;
        }
        if(outerRunning&&restarting&&(address==0x5ba42||address==0x64470))ram[0x616d8]=ram[0x616da]=0;
        if(address==entry){
            if(firstEntry)firstEntry=false;
            else{if(m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Original cockpit loop stack imbalance");m68k_get_context(savedCpu.data());stop();return;}
        }
        if(address==0x5d4ba&&!boundarySeen){
            boundarySeen=true;outerRunning=true;m68k_get_context(boundaryCpu.data());stop(true);return;
        }
        if(address==0x5ded0)++timerCalls;
        if(address==0x5dca6)++resultCalls;
        if(address==0x60824)++fuelCalls;
        if(address==0x5e12a)++distanceCalls;
        if(address==0x5dd80)++lapCounterCalls;
        if(address==0x5e23e)++speedCalls;
        if(outerRunning){
            if(address==0x5d402){restarting=true;++restartUpdates;}
            if(address==0x6488a||address==0x5d674){stop();return;}
        }
    }
}catch(const std::exception& e){failure=e.what();invalid=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>3000000){invalid=true;m68k_end_timeslice();}
}
unsigned read(unsigned address){
    if(address<ram.size())return ram[address];
    if(address>=stackBase&&address<stackTop)return stack[address-stackBase];
    if(address==0xbfe001)return cia;if(address==0xbfe201)return ddra;
    if(address==0xdff00c)return joy>>8;if(address==0xdff00d)return joy&255;
    if(address>=stopPc&&address<stopPc+4)return address&1?0x71:0x4e;
    if(address==0xdff002||address==0xdff003)return 0;
    badAddress=address;invalid=true;return 0;
}
void write(unsigned address,unsigned value){
    if(address<ram.size()){ram[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stack[address-stackBase]=std::uint8_t(value);return;}
    if(address==0xbfe201){ddra=value;return;}
    if(address>=0xdff000&&address<0xdff200)return;
    badAddress=address;invalid=true;
}
std::vector<std::uint8_t> readFile(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing cockpit input "+path.string());
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void runLeaf(unsigned address,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    const auto interruptedStack=stack;
    leaf=true;leafEntry=address;done=invalid=false;instructions=badAddress=0;failure.clear();
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(stopPc>>(24-8*n));
    m68k_set_reg(M68K_REG_PC,address);m68k_execute(80000000);leaf=false;
    if(invalid||!done)throw std::runtime_error("Original cockpit leaf failed entry="+std::to_string(address)+" pc="+std::to_string(pc)+" address="+std::to_string(badAddress)+" "+failure);
    stack=interruptedStack;
    originalInstructions+=instructions;
}
std::array<std::uint8_t,320*32> pixels(const Ram& memory,unsigned base){
    std::array<std::uint8_t,320*32> result{};
    for(unsigned row=0;row<32;++row)for(unsigned x=0;x<320;++x){
        unsigned index=0;for(unsigned plane=0;plane<4;++plane)
            if(memory.at(base+8000*plane+(168+row)*40+x/8)&(128u>>(x&7)))index|=1u<<plane;
        result[row*320+x]=std::uint8_t(index);
    }
    return result;
}
std::array<std::uint8_t,320*32> pixels(unsigned base){return pixels(ram,base);}
void compareHud(){
    const unsigned base=wheel::longword(ram.data(),0x6a584);
    for(unsigned buffer=0;buffer<2;++buffer){
        const auto actual=pixels(base+32000*buffer);
        if(buffer==0&&actual!=boundaryHud)++postOuterHudChanges;
        if(actual!=frame->instrumentPixels){
            const auto n=unsigned(std::mismatch(actual.begin(),actual.end(),frame->instrumentPixels.begin()).first-actual.begin());
            std::ostringstream s;s<<"Cockpit HUD pixel differs buffer="<<buffer<<" x="<<n%320<<" y="<<168+n/320<<" native="<<unsigned(frame->instrumentPixels[n])<<" original="<<unsigned(actual[n])<<" restarting="<<restarting;
            throw std::runtime_error(s.str());
        }
        hudPixels+=actual.size();
    }
}
void compareWheel(unsigned index){
    const auto original=wheel::readState(ram.data());const auto& expected=frame->wheelTimeline[index];
    // Wait bytes are device service state; the original main-loop boundary
    // may already have consumed them. The animation accumulator, phase and
    // actual Copper sprite pointers are the presentation contract.
    if(original.accumulator!=expected.accumulator||original.phase!=expected.phase||original.spritePointers!=expected.spritePointers){
        std::ostringstream s;s<<"Cockpit wheel timeline differs index="<<index<<" accumulator="<<unsigned(expected.accumulator)<<'/'<<unsigned(original.accumulator)<<" phase="<<unsigned(expected.phase)<<'/'<<unsigned(original.phase)<<" rightPointer="<<expected.spritePointers[0]<<'/'<<original.spritePointers[0]<<" leftPointer="<<expected.spritePointers[1]<<'/'<<original.spritePointers[1];throw std::runtime_error(s.str());
    }
    ++wheelStates;
}
void compareSprites(){
    // Apply the actual original pending Copper-interrupt header update before
    // reading the hardware positions. Native capture applies the same pending
    // update logically, without writing into gameplay RAM.
    if(ram[0x69f41]){runLeaf(0x69f04);ram[0x69f41]=0;}
    const auto displayTop=wheel::word(ram.data(),0x69ec4);
    for(unsigned i=0;i<12;++i){
        const auto address=wheel::longword(ram.data(),0x6a4ac+4*(37+i));
        const auto pos=wheel::word(ram.data(),address),ctl=wheel::word(ram.data(),address+2);
        const unsigned start=(pos>>8)|((ctl&4)<<6),end=(ctl>>8)|((ctl&2)<<7);
        const int x=int(((pos&255)<<1)|(ctl&1))-0x80,y=int(start)-int(displayTop);
        const auto& expected=frame->wheelSprites.placements[i];
        if(expected.x!=x||expected.y!=y||expected.rows!=((end-start)&511)){
            std::ostringstream s;s<<"Cockpit wheel sprite header differs id="<<37+i<<" native="<<expected.x<<','<<expected.y<<','<<expected.rows<<" original="<<x<<','<<y<<','<<((end-start)&511);throw std::runtime_error(s.str());
        }
        ++spritePlacements;
    }
}
void compareFrameEffects(){
    if(!originalEffects.rendered||!originalEffects.messageCaptured)throw std::runtime_error("Original loop missed race-effect presentation checkpoints");
    if(frame->particleFrameCounter!=originalEffects.frameCounter)throw std::runtime_error("Particle frame-counter snapshot differs");
    const auto& actual=originalEffects.particles;const auto& expected=frame->particleDraws;
    if(actual.size()!=expected.size()){
        std::ostringstream s;s<<"Particle draw count differs native="<<expected.size()<<" original="<<actual.size()<<" restarting="<<restarting;throw std::runtime_error(s.str());
    }
    for(unsigned i=0;i<actual.size();++i){
        const auto& a=actual[i];const auto& e=expected[i];
        if(a.x!=e.x||a.y!=e.y||a.index!=e.index||a.dust!=e.dust){
            std::ostringstream s;s<<"Particle draw event differs event="<<i<<" native="<<e.x<<','<<e.y<<','<<unsigned(e.index)<<','<<e.dust<<" original="<<a.x<<','<<a.y<<','<<unsigned(a.index)<<','<<a.dust<<" restarting="<<restarting;throw std::runtime_error(s.str());
        }
    }
    const auto& a=originalEffects.message;const auto& e=frame->message;
    const std::array<unsigned,10> actualMessage{{a.active,a.countdown,a.pause,a.blink,a.index,a.mode,a.panel.x,a.panel.y,a.panel.widthWords,a.panel.height}};
    const std::array<unsigned,10> expectedMessage{{e.active,e.countdown,e.pause,e.blink,e.index,e.mode,e.panel.x,e.panel.y,e.panel.widthWords,e.panel.height}};
    constexpr std::array<const char*,10> names{{"active","countdown","pause","blink","index","mode","panel.x","panel.y","panel.widthWords","panel.height"}};
    if(actualMessage!=expectedMessage){
        const auto i=unsigned(std::mismatch(actualMessage.begin(),actualMessage.end(),expectedMessage.begin()).first-actualMessage.begin());
        std::ostringstream s;s<<"Race-message snapshot differs field="<<names[i]<<" native="<<expectedMessage[i]<<" original="<<actualMessage[i]<<" restarting="<<restarting;throw std::runtime_error(s.str());
    }
    // Observe the actual original gate's admitted path, independently of the
    // native visible() predicate and of subsequent timing/outer-loop writes.
    if(e.visible()!=originalEffects.messageVisible)throw std::runtime_error("Race-message visibility differs from original executed gate");
    ++effectCoverage.frames;++effectCoverage.messageSnapshots;
    if(restarting)++effectCoverage.restartFrames;
    if(originalEffects.messageVisible){
        ++effectCoverage.visibleMessages;
        if(a.index==0x2c)++effectCoverage.dropStart;
        if(a.index==0x3c)++effectCoverage.pressFire;
    }
    effectCoverage.particleDraws+=actual.size();
    for(const auto& particle:actual){if(particle.dust)++effectCoverage.dustDraws;else ++effectCoverage.sparkDraws;}
}
void effectReport(std::ostream& out,const EffectCoverage& first={}){
    out<<",\"effect_frame_snapshots\":"<<effectCoverage.frames-first.frames
       <<",\"race_message_snapshots\":"<<effectCoverage.messageSnapshots-first.messageSnapshots
       <<",\"visible_race_message_frames\":"<<effectCoverage.visibleMessages-first.visibleMessages
       <<",\"drop_start_frames\":"<<effectCoverage.dropStart-first.dropStart
       <<",\"press_fire_frames\":"<<effectCoverage.pressFire-first.pressFire
       <<",\"particle_draw_events\":"<<effectCoverage.particleDraws-first.particleDraws
       <<",\"dust_draw_events\":"<<effectCoverage.dustDraws-first.dustDraws
       <<",\"spark_draw_events\":"<<effectCoverage.sparkDraws-first.sparkDraws
       <<",\"restart_effect_frame_snapshots\":"<<effectCoverage.restartFrames-first.restartFrames;
}
void prepareOriginalHud(){
    // Reconstruct original race-entry drawing for retained old-format seeds.
    // Its clock/lap state has already undergone the ordinary two warm-ups.
    runLeaf(0x1bace);runLeaf(0x5981a,14);runLeaf(0x59806,0);
    runLeaf(0x5d87e);runLeaf(0x5e474);runLeaf(0x5e0d6,0,0,11);runLeaf(0x5e0aa,0,0,11);
    wheel::putWord(ram.data(),0x1bc20,0x7f);runLeaf(0x5e23e);
    runLeaf(0x696fc);
}
void runMainSegment(){
    done=invalid=atBoundary=atWait=false;instructions=badAddress=0;failure.clear();m68k_execute(80000000);
    if(invalid||!done){std::ostringstream s;s<<"Original cockpit loop failed pc=0x"<<std::hex<<pc<<" address=0x"<<badAddress<<std::dec<<" instructions="<<instructions<<" "<<failure;throw std::runtime_error(s.str());}
    originalInstructions+=instructions;
}
std::vector<std::string> fields(const std::string& line){
    std::vector<std::string> result;std::size_t start=0;
    for(;;){const auto end=line.find('\t',start);result.push_back(line.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
    return result;
}
std::vector<Scenario> manifestScenarios(const std::filesystem::path& path,unsigned maximumOverride){
    std::ifstream in(path);std::string line;
    if(!std::getline(in,line))throw std::invalid_argument("Cannot read cockpit scenario manifest");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="name\tseed\tcontrols\tmaximum\tpreserve_hud")throw std::invalid_argument("Unexpected cockpit scenario manifest header");
    std::vector<Scenario> scenarios;
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        const auto row=fields(line);if(row.size()!=5)throw std::invalid_argument("Cockpit scenario manifest needs five tab-separated fields");
        if(row[0].empty()||row[0].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::invalid_argument("Unsafe cockpit scenario name");
        if(row[4]!="0"&&row[4]!="1")throw std::invalid_argument("preserve_hud must be 0 or 1");
        Scenario scenario{row[0],std::filesystem::path(row[1]),std::filesystem::path(row[2]),maximumOverride?maximumOverride:unsigned(std::stoul(row[3])),row[4]=="1"};
        if(!scenario.maximum)throw std::invalid_argument("Cockpit scenario maximum must be positive");
        if(scenario.seed.is_relative())scenario.seed=path.parent_path()/scenario.seed;
        if(scenario.controls.is_relative())scenario.controls=path.parent_path()/scenario.controls;
        scenarios.push_back(std::move(scenario));
    }
    if(scenarios.empty())throw std::invalid_argument("Empty cockpit scenario manifest");return scenarios;
}
} // namespace
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    std::vector<Scenario> scenarios;std::filesystem::path payloadPath;
    if(argc>=2&&std::string(argv[1])=="--manifest"){
        if(argc<4||argc>5)throw std::invalid_argument("Usage: native_cockpit_replay_oracle --manifest scenarios.tsv payload.bin [maximum-override]");
        scenarios=manifestScenarios(argv[2],argc==5?unsigned(std::stoul(argv[4])):0);payloadPath=argv[3];
    }else{
        if(argc<2||argc>3)throw std::invalid_argument("Usage: native_cockpit_replay_oracle integration-directory [maximum-ticks-per-track]");
        const std::filesystem::path directory=argv[1];const unsigned maximum=argc==3?unsigned(std::stoul(argv[2])):128;
        payloadPath=directory/"source-payload.bin";
        for(unsigned track=0;track<8;++track){const auto name="track-"+std::to_string(track);const auto base=directory/name;scenarios.push_back({name,base/"seed.bin",base/"controls.csv",maximum,false});}
    }
    const auto payload=readFile(payloadPath);std::ostringstream scenarioReport;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    savedCpu.resize(m68k_context_size());boundaryCpu.resize(m68k_context_size());
    for(unsigned scenarioIndex=0;scenarioIndex<scenarios.size();++scenarioIndex){
        const auto& scenario=scenarios[scenarioIndex];currentTrack=scenarioIndex;currentScenario=scenario.name;
        auto seed=replay::loadCapture(scenario.seed.string().c_str());const auto controls=replay::loadInputs(scenario.controls.string().c_str());
        ram=*seed;
        if(scenario.preserveHud)runLeaf(0x696fc);
        else{
            // Current native startup stores its initialized foreground masks
            // and both HUD buffers. Retained pre-cockpit seeds have zero
            // masks; they remain usable as reconstruction regression inputs.
            const bool preparedSeed=std::any_of(seed->begin()+0x5982e,seed->begin()+0x5983e,[](auto b){return b!=0;});
            prepareOriginalHud();
            if(preparedSeed){
                const auto first=wheel::longword(ram.data(),0x6a584);
                for(unsigned screen=0;screen<2;++screen){
                    const auto actual=pixels(first+32000*screen),expected=pixels(*seed,first+32000*screen);
                    if(actual!=expected){const auto n=unsigned(std::mismatch(actual.begin(),actual.end(),expected.begin()).first-actual.begin());std::ostringstream s;s<<"Fresh native startup HUD differs buffer="<<screen<<" x="<<n%320<<" y="<<168+n/320<<" native="<<unsigned(expected[n])<<" original="<<unsigned(actual[n]);throw std::runtime_error(s.str());}
                    startupHudPixels+=actual.size();
                }
                ++startupHudSeeds;
            }
        }
        *seed=ram;
        auto scene=std::make_unique<scr::viewer::ReplayScene>();scene->loadNativeState(*seed,payload,scenario.seed.parent_path()/"cockpit-oracle-recordings");cia=ddra=255;
        const auto firstTick=ticks,firstTimer=timerCalls,firstResults=resultCalls,firstCounter=lapCounterCalls,firstFuel=fuelCalls,firstRestart=restartUpdates;
        const auto firstEffects=effectCoverage;
        for(unsigned tick=0;tick<std::min(scenario.maximum,unsigned(controls.size()));++tick){
            currentTick=tick;scene->stepLive(controls[tick].controls,controls[tick].quit);frame=&scene->frames().back();
            const auto hardware=replay::hardware(controls[tick].controls,std::uint8_t(ddra));joy=hardware.joy1dat;cia=hardware.ciaPortA;
            std::fill_n(ram.begin()+0xead6,128,std::uint8_t{});ram[0xead6+0x45]=controls[tick].quit?0xb3:0;
            if(!tick){
                m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
                m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop);m68k_set_reg(M68K_REG_PC,entry);
            }else m68k_set_context(savedCpu.data());
            firstEntry=true;outerRunning=restarting=boundarySeen=false;originalEffects={};runMainSegment();
            if(!atBoundary)throw std::runtime_error("Original loop missed cockpit boundary");
            boundaryHud=pixels(wheel::longword(ram.data(),0x6a584));
            compareWheel(0);unsigned phaseIndex=0;
            do {
                m68k_set_context(boundaryCpu.data());runMainSegment();
                if(atWait){
                    while(ram[0x616d8]){
                        runLeaf(wheel::vblankEntry);++prefixCalls;
                        if(phaseIndex<6)compareWheel(++phaseIndex);
                    }
                }
            }while(atWait);
            if(phaseIndex!=std::min(6u,frame->wheelVblanks))throw std::runtime_error("Native/original wheel wait count differs");
            compareSprites();compareHud();compareFrameEffects();++ticks;
            if(scene->liveTerminal())break;
        }
        if(scenarioIndex)scenarioReport<<',';
        scenarioReport<<"{\"name\":\""<<scenario.name<<"\",\"ticks\":"<<ticks-firstTick<<",\"timer_calls\":"<<timerCalls-firstTimer<<",\"results_calls\":"<<resultCalls-firstResults<<",\"lap_counter_calls\":"<<lapCounterCalls-firstCounter<<",\"fuel_calls\":"<<fuelCalls-firstFuel<<",\"restarts\":"<<restartUpdates-firstRestart<<",\"preserve_hud\":"<<(scenario.preserveHud?"true":"false");
        effectReport(scenarioReport,firstEffects);scenarioReport<<'}';
    }
    std::cout<<"{\"scenarios\":["<<scenarioReport.str()<<"],\"ticks\":"<<ticks<<",\"hud_pixels\":"<<hudPixels<<",\"startup_hud_seeds\":"<<startupHudSeeds<<",\"startup_hud_pixels\":"<<startupHudPixels<<",\"wheel_timeline_states\":"<<wheelStates<<",\"sprite_placements\":"<<spritePlacements<<",\"original_vblank_prefix_calls\":"<<prefixCalls<<",\"restart_updates\":"<<restartUpdates<<",\"post_outer_hud_changes\":"<<postOuterHudChanges
             <<",\"timer_calls\":"<<timerCalls<<",\"result_calls\":"<<resultCalls<<",\"fuel_calls\":"<<fuelCalls<<",\"distance_calls\":"<<distanceCalls<<",\"lap_counter_calls\":"<<lapCounterCalls<<",\"speed_calls\":"<<speedCalls<<",\"original_instructions\":"<<originalInstructions;
    effectReport(std::cout);
    std::cout<<",\"mismatches\":0,\"original_drawing_calls_replaced\":false,\"original_code_modified\":false,\"independent_evolving_native_scene\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"native_cockpit_replay_oracle: "<<e.what()<<" scenario="<<currentScenario<<" index="<<currentTrack<<" tick="<<currentTick<<'\n';return 1;}}

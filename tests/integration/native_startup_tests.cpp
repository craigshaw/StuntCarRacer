#include "native_startup.h"
#include "replay_scene.h"
#include "game_audio.h"
#include "native_damage_holes.h"
#include "driving_replay_fixture.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::uint64_t hash(const std::vector<std::uint32_t>& pixels){std::uint64_t h=14695981039346656037ULL;for(auto p:pixels){h^=p;h*=1099511628211ULL;}return h;}
std::vector<std::uint8_t> read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void write(const std::filesystem::path& path,const std::uint8_t* data,std::size_t size){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data),std::streamsize(size));if(!f)throw std::runtime_error("Test artifact write failed");}
void advance(scr::test::replay::NativeReplay& state,unsigned mask){
    state.begin(mask);state.controlsStage();state.physicsStage();state.engineStage();state.progressStage();
    state.groundStage();state.wheelsStage();state.bridgeStage();state.damageStage();state.timingStage();state.outerStage();
}
void steeringRegression(const std::filesystem::path& pack){
    using scr_test::readWord;
    auto startup=scr::viewer::startNativeRace(pack,0);
    auto driven=std::make_unique<scr::test::replay::NativeReplay>(*startup.memory);
    bool ready=false;
    for(unsigned tick=0;tick<600;++tick){
        advance(*driven,1);
        const auto speed=readWord(driven->ram,0x1bd30);
        if(!driven->ram[0x1bbdf]&&driven->ram[0x1bb7e]&&speed>=0x200&&speed<0x4000){ready=true;break;}
    }
    require(ready,"Fresh race never reached a moving, steerable state");
    auto left=std::make_unique<scr::test::replay::NativeReplay>(driven->ram);
    auto right=std::make_unique<scr::test::replay::NativeReplay>(driven->ram);
    auto neutral=std::make_unique<scr::test::replay::NativeReplay>(driven->ram);
    advance(*left,5);advance(*right,9);advance(*neutral,1);
    require(left->ram[0x1bb47]==5&&right->ram[0x1bb47]==9,"Fresh steering inputs did not reach gameplay");
    require(readWord(left->ram,0x1bcfe)!=readWord(right->ram,0x1bcfe),"Fresh left/right inputs produce identical yaw acceleration");
    for(unsigned tick=0;tick<3;++tick){advance(*left,5);advance(*right,9);advance(*neutral,1);}
    require(readWord(left->ram,0x1bce6)!=readWord(right->ram,0x1bce6),"Fresh left/right inputs do not turn the car");
    const auto relative=[&](const auto& state){const auto bits=std::uint16_t(readWord(state.ram,0x1bce6)-readWord(neutral->ram,0x1bce6));return bits<0x8000?int(bits):int(bits)-65536;};
    require(relative(*left)*relative(*right)<0,"Fresh left/right inputs must turn to opposite sides of neutral");
}
unsigned damageEntryRegression(const std::filesystem::path& pack){
    // Persistent configuration is prepared before race entry, never midrace.
    unsigned entries=0;
    for(unsigned mode:{0u,1u,0x80u})for(unsigned holes:{0u,4u,10u}){
        auto startup=scr::viewer::startNativeRace(pack,0);auto& memory=*startup.memory;
        memory[0x5eb76]=1;memory[0x5eb79]=11;memory[0x1ca22]=std::uint8_t(mode);
        memory[0x1c9cf]=10;memory[0x1ca36+11]=std::uint8_t(holes);
        scr::viewer::initializeNativeRace(memory);
        require(memory[0x1bbb5]==10&&memory[0x1c9cf]==holes,"Race entry did not restore this driver's damage allowance");
        const auto actual=scr::viewer::captureCockpitFrame(memory.data(),memory.size());
        scr::viewer::initializeNativeDamageHoles(memory);
        const auto expected=scr::viewer::captureCockpitFrame(memory.data(),memory.size());
        require(actual.damageStrip==expected.damageStrip,"Race entry omitted carried damage pixels");++entries;
    }
    return entries;
}
void reviewInterval(scr::viewer::ReplayScene& scene,const std::filesystem::path& folder,const char* name){
    using namespace scr::viewer;
    const auto& a=scene.frames().front();const auto& b=scene.frames().back();
    require(canInterpolateReplay(a.restarts,b.restarts,a.action,b.action),"Review interval crossed a transition");
    const auto exact=scene.render(1);
    const auto output=folder/name;std::filesystem::create_directories(output);
    for(const auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen}){
        const auto prefix=std::to_string(unsigned(view))+"-";
        for(unsigned n=0;n<=8;++n){scene.renderLive(n/8.0,false,true,view);scene.saveBitmap(output/(prefix+std::to_string(n)+".bmp"));}
        const auto midpoint=scene.renderLive(0.5,false,true,view);
        scene.renderLive(0.875,false,true,view);scene.renderLive(0.125,false,true,view);
        require(scene.renderLive(0.5,false,true,view)==midpoint,"Reverse effect queries changed their result");
    }
    require(scene.render(1)==exact,"Effect review changed the exact endpoint");
}
struct RaceEffectsCoverage {unsigned ticks{},dropStart{},pressFire{},dust{},sparks{},smoothDust{},smoothSparks{},smoothWheels{};};
struct BoostCoverage {unsigned ticks{},frames{},phases{},releaseFrames{},chainFrames{},liftingFrames{},smoothChains{},chainExit{};};
BoostCoverage boostRegression(const std::filesystem::path& pack,const std::filesystem::path& output){
    using namespace scr::viewer;
    const auto folder=output/"boost-effects";std::filesystem::create_directories(folder);
    auto startup=startNativeRace(pack,0);write(folder/"native-initial.bin",startup.memory->data(),startup.memory->size());
    ReplayScene scene;scene.loadNativeState(*startup.memory,startup.payload,folder/"recordings");
    BoostCoverage coverage;std::ofstream controls(folder/"controls.csv");controls<<"tick,controls\n";
    for(unsigned tick=0;tick<180;++tick){
        const unsigned mask=(tick>=55&&tick<70)?1:17;controls<<tick<<','<<mask<<'\n';
        const auto previous=scene.frames().back().cockpit.flameFrame;
        require(scene.stepLive(mask),"Boost effects drive unexpectedly ended");++coverage.ticks;
        const auto& frame=scene.frames().back();
        const auto phase=frame.cockpit.flameFrame;
        const auto exact=hash(scene.render(1));
        if(frame.cockpit.boostActive){
            if(!(coverage.phases&(1u<<phase)))scene.saveBitmap(folder/("flame-"+std::to_string(phase)+".bmp"));
            coverage.phases|=1u<<phase;++coverage.frames;
        }else if(tick>=55&&tick<70){
            require(phase==previous,"Releasing boost did not hold the animation phase");++coverage.releaseFrames;
        }
        if(!frame.chainDraws.empty()){
            if(!coverage.chainFrames)scene.saveBitmap(folder/"chains-hanging.bmp");
            ++coverage.chainFrames;
            if(frame.chains.position<0xb0){
                if(!coverage.liftingFrames)scene.saveBitmap(folder/"chains-lifting.bmp");
                ++coverage.liftingFrames;
            }
        }
        const auto& from=scene.frames().front();
        if(canInterpolateReplay(from.restarts,frame.restarts,from.action,frame.action)){
            if(!from.chainDraws.empty()&&!frame.chainDraws.empty()&&from.chains.position!=frame.chains.position){
                if(!coverage.smoothChains)reviewInterval(scene,folder,"chains-lifting");++coverage.smoothChains;
            }
            if(!from.chainDraws.empty()&&frame.chainDraws.empty()){
                if(!coverage.chainExit)reviewInterval(scene,folder,"chains-exit");++coverage.chainExit;
            }
        }
        scene.renderLive(0.5);scene.render(0);scene.renderLive(0.25);scene.renderView(1,{});
        require(hash(scene.render(1))==exact,"View queries changed boost/chain animation");
    }
    controls.flush();
    require(coverage.phases==7&&coverage.releaseFrames==15&&coverage.chainFrames&&coverage.liftingFrames&&coverage.smoothChains&&coverage.chainExit,
        "Natural boost drive missed flame phases, release or chain lift");
    const auto recording=scene.liveRecording().string();
    const auto trace=recording.substr(0,recording.size()-std::string("-controls.csv").size())+"-native.csv";
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
    return coverage;
}
RaceEffectsCoverage effectsRegression(const std::filesystem::path& pack,const std::filesystem::path& output){
    using namespace scr::viewer;
    const auto folder=output/"race-effects";std::filesystem::create_directories(folder);
    auto startup=startNativeRace(pack,0);write(folder/"native-initial.bin",startup.memory->data(),startup.memory->size());
    ReplayScene scene,control;scene.loadNativeState(*startup.memory,startup.payload,folder/"recordings");
    control.loadNativeState(*startup.memory,startup.payload,{});
    RaceEffectsCoverage coverage;bool savedDrop=false,savedRecovery=false,savedDust=false,savedSparks=false;
    std::ofstream controls(folder/"controls.csv");controls<<"tick,controls\n";
    for(unsigned tick=0;tick<1600;++tick){
        // A fresh deterministic drive: accelerate, then steer off the track.
        // No car position, collision state, particle or RNG injection.
        const auto& current=scene.frames().back();
        const bool recovering=current.message.visible()&&current.message.index==0x3c;
        const unsigned mask=(tick<150?1:9)|(recovering?16:0);controls<<tick<<','<<mask<<'\n';
        require(scene.stepLive(mask),"Race effects drive unexpectedly ended");++coverage.ticks;
        require(control.stepLive(mask),"Control race ended before the viewed race");
        const auto& frame=scene.frames().back();
        const bool drop=frame.message.visible()&&frame.message.index==0x2c;
        const bool recovery=frame.message.visible()&&frame.message.index==0x3c;
        bool dust=false,sparks=false;for(const auto& draw:frame.particleDraws){dust|=draw.dust;sparks|=!draw.dust;}
        coverage.dropStart+=drop;coverage.pressFire+=recovery;coverage.dust+=dust;coverage.sparks+=sparks;
        const auto exact=hash(scene.render(1));
        if(drop&&!savedDrop){scene.saveBitmap(folder/"drop-start.bmp");savedDrop=true;}
        if(recovery&&!savedRecovery){scene.saveBitmap(folder/"press-fire.bmp");savedRecovery=true;}
        if(dust&&!savedDust&&coverage.dust>=6){scene.saveBitmap(folder/"dust.bmp");savedDust=true;}
        if(sparks&&!savedSparks){scene.saveBitmap(folder/"sparks.bmp");savedSparks=true;}
        const auto& from=scene.frames().front();
        if(canInterpolateReplay(from.restarts,frame.restarts,from.action,frame.action)){
            const auto particles=interpolateNativeDrivingParticles(from.particleDraws,frame.particleDraws,0.5);
            bool movingDust=false,movingSpark=false;
            for(unsigned n=0;n<particles.size();++n)if(particles[n].x!=from.particleDraws[n].x||particles[n].y!=from.particleDraws[n].y){
                movingDust|=particles[n].dust;movingSpark|=!particles[n].dust;
            }
            if(movingDust){if(!coverage.smoothDust)reviewInterval(scene,folder,"dust-moving");++coverage.smoothDust;}
            if(movingSpark){if(!coverage.smoothSparks)reviewInterval(scene,folder,"sparks-moving");++coverage.smoothSparks;}
            bool wheels=false;
            for(unsigned n=0;n<12;++n)wheels|=std::abs(from.wheelSprites.placements[n].y-frame.wheelSprites.placements[n].y)>=4;
            if(wheels){if(!coverage.smoothWheels)reviewInterval(scene,folder,"wheels-moving");++coverage.smoothWheels;}
        }
        scene.renderLive(0.5);scene.render(0);scene.renderLive(0.25);scene.renderView(1,{});
        require(hash(scene.render(1))==exact,"Interpolated or reverse views contaminated race effects");
        require(scene.render(1)==control.render(1),"Effect queries changed a later simulation frame");
        if(coverage.pressFire>4&&coverage.dust>8&&coverage.smoothSparks&&coverage.smoothDust&&coverage.smoothWheels)break;
    }
    controls.flush();
    require(coverage.dropStart&&coverage.pressFire&&coverage.dust&&coverage.sparks,"Natural drive did not exercise start/recovery messages, dust and sparks");
    if(!coverage.smoothDust||!coverage.smoothSparks||!coverage.smoothWheels)
        throw std::runtime_error("Fresh drive missed interpolation: dust="+std::to_string(coverage.smoothDust)+
            " sparks="+std::to_string(coverage.smoothSparks)+" wheels="+std::to_string(coverage.smoothWheels)+" spark frames="+std::to_string(coverage.sparks));
    const auto recording=scene.liveRecording().string();
    const auto trace=recording.substr(0,recording.size()-std::string("-controls.csv").size())+"-native.csv";
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
    for(unsigned n=0;n<600&&!scene.liveTerminal();++n){
        require(scene.stepLive(0,true)==control.stepLive(0,true),"Effect queries changed the quit transition");
    }
    require(scene.liveTerminal()&&control.liveTerminal(),"Effects isolation drive did not finish");
    auto actual=std::make_unique<NativeMemory>(),expected=std::make_unique<NativeMemory>();
    scene.copyLiveExit(*actual);control.copyLiveExit(*expected);
    require(*actual==*expected,"Effect interpolation changed gameplay, RNG or exit memory");
    return coverage;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("native_startup_tests pack output-directory");
    using namespace scr::viewer;const std::filesystem::path pack=argv[1],out=argv[2];std::filesystem::create_directories(out);
    const auto original=read(pack);unsigned rejected=0;const auto bad=out/"invalid.scr-assets";
    for(unsigned offset:{0u,8u,12u,16u,48u,80u,84u,88u,unsigned(original.size()-1)}){
        auto bytes=original;bytes.at(offset)^=1;write(bad,bytes.data(),bytes.size());
        try{(void)startNativeRace(bad);}catch(const std::runtime_error&){++rejected;}
    }
    write(bad,original.data(),original.size()-1);try{(void)startNativeRace(bad);}catch(const std::runtime_error&){++rejected;}
    require(rejected==10,"Corrupt asset pack was accepted");
    steeringRegression(pack);
    const auto damageEntries=damageEntryRegression(pack);
    unsigned ticks=0,rendered=0,starts=0;std::uint16_t maximumSpeed=0;
    for(unsigned track=0;track<8;++track){
        auto startup=startNativeRace(pack,track);const auto trackOut=out/("track-"+std::to_string(track));std::filesystem::create_directories(trackOut);
        require((*startup.memory)[0x1ca1a]>0&&(*startup.memory)[0x1ca1a]<=100,"Fresh track topology missing");
        require((*startup.memory)[0x1bb55]==0&&(*startup.memory)[0x1bb20]==0,"Fresh race inherited damage/laps");
        write(trackOut/"native-initial.bin",startup.memory->data(),startup.memory->size());
        ReplayScene scene;scene.loadNative(pack,trackOut/"recordings",track);const auto initial=hash(scene.renderLive(0));
        const auto recording=scene.liveRecording();
        GameAudio audio(scene.audioSeed());std::ofstream controls(trackOut/"controls.csv");controls<<"tick,controls\n";
        const auto initialPosition=scene.frames().back().pose.worldPosition;bool moved=false,changed=false;
        for(unsigned tick=0;tick<512;++tick){
            const unsigned mask=tick<380?1:(tick<440?9:2);controls<<tick<<','<<mask<<'\n';
            require(scene.stepLive(mask),"Fresh race stopped unexpectedly");++ticks;
            const auto& frame=scene.frames().back();moved|=frame.pose.worldPosition[0]!=initialPosition[0]||frame.pose.worldPosition[2]!=initialPosition[2];
            const int speed=frame.forwardSpeed<0x8000?int(frame.forwardSpeed):int(frame.forwardSpeed)-65536;
            maximumSpeed=std::max(maximumSpeed,std::uint16_t(speed<0?-speed:speed));
            changed|=hash(scene.renderLive(0.5))!=initial;++rendered;
            if(tick<64){audio.submit(frame.audio);const auto pcm=audio.renderFrames(5760);require(!pcm.empty(),"Fresh audio produced no PCM");}
            if(scene.liveTerminal())throw std::runtime_error("Fresh Practice reached unexpected terminal state");
        }
        require(moved&&changed,"Fresh race did not evolve or render");
        scene.resetLive();require(hash(scene.renderLive(0))==initial,"Reset did not reconstruct deterministic fresh state");++starts;
        const auto name=recording.string();const std::filesystem::path trace=name.substr(0,name.size()-std::string("-controls.csv").size())+"-native.csv";
        std::filesystem::copy_file(trace,trackOut/"native.csv",std::filesystem::copy_options::overwrite_existing);
    }
    require(maximumSpeed>0x100,"Fresh vehicle did not accelerate");
    require(read(pack)==original,"Native startup modified its asset pack");
    const auto effects=effectsRegression(pack,out);
    const auto boost=boostRegression(pack,out);
    std::cout<<"{\"status\":\"passed\",\"fresh_starts\":"<<starts<<",\"ticks\":"<<ticks<<",\"rendered_frames\":"<<rendered
             <<",\"corrupt_packs_rejected\":"<<rejected<<",\"maximum_speed_word\":"<<maximumSpeed
             <<",\"effects_ticks\":"<<effects.ticks<<",\"drop_start_frames\":"<<effects.dropStart<<",\"press_fire_frames\":"<<effects.pressFire
             <<",\"dust_frames\":"<<effects.dust<<",\"spark_frames\":"<<effects.sparks<<",\"effects_view_isolation\":true"
             <<",\"smooth_dust_intervals\":"<<effects.smoothDust<<",\"smooth_spark_intervals\":"<<effects.smoothSparks<<",\"smooth_wheel_intervals\":"<<effects.smoothWheels
             <<",\"boost_effects_ticks\":"<<boost.ticks<<",\"boost_flame_frames\":"<<boost.frames<<",\"boost_flame_phase_mask\":"<<boost.phases
             <<",\"boost_release_frames\":"<<boost.releaseFrames<<",\"chain_frames\":"<<boost.chainFrames<<",\"chain_lifting_frames\":"<<boost.liftingFrames
             <<",\"smooth_chain_intervals\":"<<boost.smoothChains<<",\"chain_exit_intervals\":"<<boost.chainExit<<",\"effect_simulation_memory_identical\":true"
             <<",\"multiplayer_damage_entries\":"<<damageEntries
             <<",\"capture_input\":false,\"rom_input\":false,\"cpu_emulation\":false,\"deterministic_reset\":true,\"left_right_steering_checked\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "native_startup.h"
#include "replay_scene.h"
#include "game_audio.h"
#include "driving_replay_fixture.h"
#include <algorithm>
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
    state.groundStage();state.wheelsStage();state.damageStage();state.timingStage();state.outerStage();
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
    unsigned ticks=0,rendered=0,starts=0;std::uint16_t maximumSpeed=0;
    for(unsigned track:{0u,1u,2u,3u,4u,6u,7u}){
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
    std::cout<<"{\"status\":\"passed\",\"fresh_starts\":"<<starts<<",\"ticks\":"<<ticks<<",\"rendered_frames\":"<<rendered
             <<",\"corrupt_packs_rejected\":"<<rejected<<",\"maximum_speed_word\":"<<maximumSpeed
             <<",\"capture_input\":false,\"rom_input\":false,\"cpu_emulation\":false,\"deterministic_reset\":true,\"left_right_steering_checked\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

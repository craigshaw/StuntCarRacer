#include "game_audio.h"
#include "windows_audio_output.h"
#include "audio_playback_fixture.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool audible(const std::vector<std::int16_t>& pcm){return std::any_of(pcm.begin(),pcm.end(),[](auto value){return value!=0;});}
scr::viewer::GameAudioSeed load(const char* path){
    std::ifstream in(path,std::ios::binary);scr::viewer::GameAudioSeed seed;
    seed.samples={std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
    require(seed.samples.size()==524288,"Expected captured512KiB Chip RAM");
    namespace a=scr::test::audio;const auto* ram=seed.samples.data();
    seed.parameters=a::readData(ram);seed.playback=a::readState(ram);
    seed.engine={a::word(ram,0x1bc34),a::word(ram,0xf472)};seed.pitchDelta=a::word(ram,0x1bbf4);
    seed.frame={};seed.frame.dmaControl=a::word(ram,0x69ede);
    seed.frame.forwardSpeed=a::word(ram,0x1bd30);seed.frame.wheelMovement=ram[0x1bb7e];seed.frame.inputFlags=ram[0x1bb47];
    return seed;
}
}
int main(int argc,char** argv){
    try{
        if(argc<2)throw std::runtime_error("native_audio_tests capture-chip.bin [--device]");
        using namespace scr::viewer;const auto seed=load(argv[1]);const auto original=seed.samples;unsigned cases=0;
        GameAudio whole(seed),parts(seed);auto frame=seed.frame;frame.forwardSpeed=0x6800;frame.wheelMovement=1;frame.inputFlags=1;
        whole.submit(frame);parts.submit(frame);const auto expected=whole.renderFrames(5760);
        std::vector<std::int16_t> partitioned;for(unsigned count:{1u,959u,7u,1800u,2993u}){
            const auto pcm=parts.renderFrames(count);partitioned.insert(partitioned.end(),pcm.begin(),pcm.end());}
        require(expected==partitioned,"Audio depends on output buffer partition");++cases;
        require(audible(expected),"Original engine is silent");++cases;
        require(whole.statistics().vblanks==6&&whole.statistics().frames==5760,"Audio did not retain50Hz VBlank cadence");++cases;
        const auto pitch=whole.engineState().pitchAccumulator;
        frame.restarts=1;whole.submit(frame);require(whole.engineState().pitchAccumulator==pitch,"Track restart rewound asynchronous pitch");++cases;
        auto playback=seed.playback;
        for(unsigned id=0;id<7;++id){
            const auto writes=scr::requestSample(std::uint16_t(id),seed.parameters,playback);
            frame.writes={{0xdff096,0xf}};frame.writes.insert(frame.writes.end(),writes.begin(),writes.end());
            whole.submit(frame);require(audible(whole.renderFrames(5760)),"Original isolated effect is silent");
        }
        require(whole.statistics().effectStarts>=7,"Original effects were not started");++cases;
        require(seed.samples==original,"Audio changed its supplied capture");++cases;
        GameAudio accelerated(seed),idle(seed);auto fast=seed.frame,slow=seed.frame;
        fast.forwardSpeed=0x6800;fast.wheelMovement=slow.wheelMovement=1;slow.forwardSpeed=0;
        for(unsigned tick=0;tick<32;++tick){accelerated.submit(fast);idle.submit(slow);(void)accelerated.renderFrames(5760);(void)idle.renderFrames(5760);}
        require(accelerated.engineState().pitchAccumulator!=idle.engineState().pitchAccumulator,"Pitch inputs do not affect engine pitch");++cases;
        bool rejected=false;try{GameAudioSeed invalid;GameAudio bad(invalid);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid sample image was accepted");++cases;
        std::uint64_t completed=0;
        if(argc>2&&std::string(argv[2])=="--device"){
            WindowsAudioOutput output(seed);output.submit(frame);output.setPlaying(true);
            for(unsigned n=0;n<200&&output.completedBuffers()<8&&output.error().empty();++n)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            require(output.error().empty(),output.error().c_str());require(output.completedBuffers()>=8,"Device did not complete audio buffers");++cases;
            output.setPlaying(false);std::this_thread::sleep_for(std::chrono::milliseconds(100));const auto paused=output.completedBuffers();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));require(output.completedBuffers()==paused,"Paused device kept consuming game audio");++cases;
            output.setMuted(true);output.setPlaying(true);std::this_thread::sleep_for(std::chrono::milliseconds(100));
            require(output.completedBuffers()>paused,"Muted playback stopped its audio clock");++cases;
            output.reset(seed);output.setMuted(false);std::this_thread::sleep_for(std::chrono::milliseconds(100));output.setPlaying(false);
            require(output.error().empty(),output.error().c_str());completed=output.completedBuffers();++cases;
        }
        std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"device_completed_buffers\":"<<completed<<"}\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

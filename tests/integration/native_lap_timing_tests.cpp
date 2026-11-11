#include "assets/native_startup.h"
#include "race/native_race_initialization.h"
#include "race/race_scene.h"
#include "session/native_multiplayer_state.h"
#include "state/replay_io.h"
#include "state/human_record_adapter.h"
#include "track/track_initialization.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace scr;using namespace scr::runtime;namespace replay=scr::state::replay;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::array<std::uint32_t,3> advance(replay::NativeReplay& s,unsigned controls,bool quit=false){
    s.begin(controls);s.ram[0xead6+0x45]=quit?0xb3:0;
    s.controlsStage();s.physicsStage();s.engineStage();s.progressStage();
    std::array<std::uint32_t,3> position{};for(unsigned axis=0;axis<3;++axis)position[axis]=state::memory::readLong(s.ram,0x1bcd8+4*axis);
    s.groundStage();s.wheelsStage();
    s.bridgeStage();s.speedometerStage();s.damageStage();s.timingStage();s.distanceStage();s.outerStage();
    return position;
}
void compareExit(RaceScene& scene,replay::NativeReplay& reference){
    scene.quitLive();
    namespace t=scr::state::transitions;
    if(reference.outerAction!=RaceTransitionAction::raceEnd&&reference.outerAction!=RaceTransitionAction::quit){
        auto state=t::readState(reference.ram.data());reference.outerAction=quitLocalRace(t::readInputs(reference.ram.data()),state).action;t::writeState(reference.ram.data(),state);
    }
    auto expected=std::make_unique<NativeMemory>(reference.ram),actual=std::make_unique<NativeMemory>();
    if((*expected)[0x5eb76]&&((*expected)[0x1ca22]&0x80))finishNativeMultiplayerRaceExit(*expected);
    auto state=t::readState(expected->data());(void)finishLocalRaceExit(t::readInputs(expected->data()),state);t::writeState(expected->data(),state);
    scene.copyLiveExit(*actual);check(*actual==*expected,"Timing changed full simulation memory, records, progression or RNG");
}
void fresh(const std::filesystem::path& pack){
    for(bool super:{false,true})for(unsigned track=0;track<8;++track){
        auto seed=startNativeGame(pack);(*seed.memory)[0x1c9d0]=super?11:0;initializeNativeTrack(*seed.memory,track);
        if(state::drawBridge::animate(*seed.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*seed.memory);initializeNativeRace(*seed.memory);
        auto reference=std::make_unique<replay::NativeReplay>(*seed.memory);auto scene=std::make_unique<RaceScene>();scene->loadNativeState(*seed.memory,seed.payload,{});
        check(!scene->lapRecordingStatistics()&&scene->timingStatistics(),"Timing depends on an attached ghost recorder");
        for(unsigned n=0;n<512;++n){const auto position=advance(*reference,1);check(scene->stepLive(1),"Fresh timing drive ended early");
            check(scene->frames().back().pose.worldPosition==position,"Timing changed a native driving position");
            check(scene->takeTimingCompletions().empty(),"Straight fresh drive unexpectedly completed a full lap");}
        const auto stats=*scene->timingStatistics();
        check(stats.movements==512+2*reference->restarts&&stats.timingUpdates==stats.movements,"Native callback clock was flattened to outer updates");
        check(stats.recoveries==reference->restarts&&stats.recoveryObservations==2*reference->restarts,"Native recovery was double-charged or omitted");
        compareExit(*scene,*reference);
    }
}
unsigned captured(const NativeMemory& original,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& inputs){
    unsigned completions=0;
    for(unsigned mode=0;mode<4;++mode){
        auto seed=std::make_unique<NativeMemory>(original);(*seed)[0x5eb79]=std::uint8_t(mode==3?10:11);
        for(auto base:{0x1c908u,0x1c920u,0x1c938u})(*seed)[base+12+(*seed)[0x5eb79]]=0;
        if(mode>=2){
            (*seed)[0x1ca22]=0x80;(*seed)[0x5eb76]=mode==3?2:0;if(mode==3)(*seed)[0x1ca35]=1;
            auto data=std::make_unique<OpponentDrivingData>(state::opponent::readData(*seed));auto opponent=state::opponent::readState(*seed);
            opponent.segment=std::uint8_t(((*seed)[0x1ca32]+1)%(*seed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;
            initializeOpponentCoordinates(*data,state::opponent::readInputs(*seed),opponent);state::opponent::writeState(*seed,opponent);
        }
        auto scene=std::make_unique<RaceScene>();scene->loadNativeState(*seed,payload,{});
        PersonalBestLaps bests;
        if(mode==1)check(scene->recordPersonalBestLaps(bests,{{"timing",0,false},GhostLapSource::Practice}),"Practice recorder did not bind");
        auto reference=std::make_unique<replay::NativeReplay>(*seed);
        std::vector<HumanLapCrossing> nativeEvents;
        reference->presentation=[&](auto stage,const auto&,const auto& effects){if(stage==replay::ReplayPresentationStage::timing&&effects.humanLapCrossing&&effects.humanLapCrossing->completedTime)nativeEvents.push_back(*effects.humanLapCrossing);};
        unsigned laps=0,races=0;std::int64_t total=0;
        const std::array<std::int64_t,3> expected{57637644,54745506,54570949};
        for(const auto input:inputs){
            if(scene->liveTerminal())break;
            nativeEvents.clear();advance(*reference,input.controls,input.quit);check(scene->stepLive(input.controls,input.quit),"Replay timing drive ended early");
            auto completed=scene->takeTimingCompletions();check(completed.size()==nativeEvents.size(),"Timing completion stream differs from accepted native events");
            check(scene->takeTimingCompletions().empty(),"Draining timing replayed completions");
            for(unsigned n=0;n<completed.size();++n){const auto& lap=completed[n];const auto& native=nativeEvents[n];
                check(lap.counter==native.counter&&lap.original.minutes==native.completedTime->minutes&&lap.original.seconds==native.completedTime->seconds&&lap.original.subseconds==native.completedTime->subseconds,"Original corrected snapshot changed");
                check(lap.enhanced.lap.rankable()&&lap.enhanced.lap.recoveries==0,"Authored normal crossing lost confidence or acquired recovery time");
                if(mode<2)check(laps<3&&lap.enhanced.lap.elapsedMicroseconds==expected[laps],"Authored crossing timestamps differ from stage-1 evidence");
                total+=lap.enhanced.lap.scoredMicroseconds;++laps;++completions;
                if(lap.enhanced.race){++races;check(mode>=2&&lap.enhanced.race->enhanced.scoredMicroseconds==total,"Race total differs from unrounded lap accumulation");
                    const auto human=state::readHumanRaceRecord(reference->ram.data());check(human&&lap.enhanced.race->original.minutes==human->total.minutes&&lap.enhanced.race->original.seconds==human->total.seconds&&lap.enhanced.race->original.subseconds==human->total.subseconds,"Original race snapshot changed");}
            }
            if(scene->liveTicks()%120==0){const auto count=scene->timingStatistics()->movements;
                scene->renderLive(.25);scene->renderLive(.75);
                check(scene->timingStatistics()->movements==count&&scene->takeTimingCompletions().empty(),"Presentation queries advanced timing or minted a completion");}
        }
        check(laps==3&&races==(mode>=2?1u:0u),"Practice/competitive/multiplayer timing eligibility differs");
        check(scene->timingStatistics()->completions==3&&scene->timingStatistics()->rankableLaps==3,"Timing statistics omitted accepted laps");
        compareExit(*scene,*reference);
    }
    return completions;
}
}
int main(int argc,char** argv)try{
    check(argc==6,"Usage: native_lap_timing_tests pack output capture payload controls");
    fresh(argv[1]);unsigned completed=0;
    if(std::filesystem::exists(argv[3])&&std::filesystem::exists(argv[4])&&std::filesystem::exists(argv[5])){
        const auto seed=replay::loadCapture(argv[3]);std::ifstream file(argv[4],std::ios::binary);
        const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};
        completed=captured(*seed,payload,replay::loadInputs(argv[5]));
    }
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    std::ofstream report(output/"results.json");report<<"{\"status\":\"passed\",\"fresh_contexts\":16,\"native_ticks\":8192,\"captured_completions\":"<<completed<<",\"checks\":"<<checks<<",\"full_memory_and_rng_isolation\":true,\"recording_independent\":true}\n";
    std::cout<<"Native lap timing: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

#include "assets/native_startup.h"
#include "race/race_scene.h"
#include "race/native_race_initialization.h"
#include "session/native_game_session.h"
#include "state/replay_io.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace scr;using namespace scr::runtime;namespace replay=scr::state::replay;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::array<std::uint32_t,3> advance(replay::NativeReplay& state,unsigned controls){
    state.begin(controls);state.controlsStage();state.physicsStage();state.engineStage();state.progressStage();
    std::array<std::uint32_t,3> position;for(unsigned n=0;n<3;++n)position[n]=scr::state::audio::longword(state.ram.data(),0x1bcd8+4*n);
    state.groundStage();state.wheelsStage();state.bridgeStage();state.speedometerStage();state.damageStage();state.timingStage();state.distanceStage();state.outerStage();
    return position;
}
struct Counts {unsigned nativeTicks{},recoveryUpdates{},leagueLaps{},capturedLaps{},playbackQueries{},quitAfterImprovement{};};
void nativeRaces(const std::filesystem::path& pack,Counts& counts){
    for(bool super:{false,true})for(unsigned track=0;track<8;++track){
        auto game=startNativeGame(pack);(*game.memory)[0x1c9d0]=super?11:0;initializeNativeTrack(*game.memory,track);
        if(scr::state::drawBridge::animate(*game.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*game.memory);initializeNativeRace(*game.memory);
        PersonalBestLaps bests;auto scene=std::make_unique<RaceScene>();
        scene->loadNativeState(*game.memory,game.payload,{});require(scene->recordPersonalBestLaps(bests,{{"native",track,super},GhostLapSource::Practice}),"Native Practice recorder did not bind");
        auto original=std::make_unique<replay::NativeReplay>(*game.memory);
        for(unsigned tick=0;tick<512;++tick){const auto position=advance(*original,1);require(scene->stepLive(1),"Fresh recording drive ended early");
            require(scene->frames().back().pose.worldPosition==position,"Recorded driving diverged from independent native updates");++counts.nativeTicks;}
        const auto stats=scene->lapRecordingStatistics();require(stats&&stats->timingUpdates==512+stats->recoveryTimingUpdates,"Recovery timing observations were flattened to host ticks");
        counts.recoveryUpdates+=stats->recoveryTimingUpdates;
        scene->quitLive();
        // Match the public quit outcome via a second scene, then compare the
        // complete native exit. Recording must not affect game, Hall or RNG.
        auto baseline=std::make_unique<RaceScene>();baseline->loadNativeState(*game.memory,game.payload,{});
        for(unsigned tick=0;tick<512;++tick)baseline->stepLive(1);baseline->quitLive();
        auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);baseline->copyLiveExit(*b);
        require(*a==*b,"Lap recording changed native simulation, Hall or RNG");
        auto multiplayer=std::make_unique<NativeMemory>(*game.memory);(*multiplayer)[0x5eb76]=2;
        scene->loadNativeState(*multiplayer,game.payload,{});require(!scene->recordPersonalBestLaps(bests,{{"native",track,super},GhostLapSource::Practice})&&!scene->lapRecordingStatistics(),"Multiplayer bound a career recorder");
    }
    require(counts.recoveryUpdates>0,"Fresh driving did not exercise internal recovery timing");
}
void tap(NativeGameSession& game,unsigned controls=16,unsigned number=0){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(number)};game.update(0,{});game.update(0,in);game.update(0,{});}
void careerBinding(const std::filesystem::path& pack,const std::filesystem::path& root){
    NativeGameSession game(pack,{});game.enableCareers(root);game.setNavigation(NativeMenuNavigation::Responsive);game.update(0,{});
    tap(game);tap(game);require(game.enteringName(),"Career fixture did not reach name entry");
    for(unsigned char c:std::string("LAPS")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});game.update(0,in);in.rawKeys[0].pressed=false;game.update(0,in);}
    tap(game);tap(game);require(game.activeCareer()&&game.page()==NativeMenuPage::Main,"Career fixture was not registered");
    tap(game,0,2);tap(game);if(game.page()==NativeMenuPage::Division)tap(game);tap(game,0,1);tap(game);tap(game);
    require(game.page()==NativeMenuPage::Race&&game.race().lapRecordingStatistics(),"Career Practice did not automatically attach its recorder");
    for(unsigned n=0;n<32;++n)game.update(.12,{});
    NativeGameInput leave;leave.leaveRace=true;game.update(0,leave);game.update(0,{});
    // Responsive Back returns Track -> Division -> the same career main menu.
    NativeGameInput back;back.menu.back=true;
    for(unsigned n=0;n<4&&game.page()!=NativeMenuPage::Main;++n){game.update(0,{});game.update(0,back);game.update(0,{});}
    require(game.page()==NativeMenuPage::Main,"Practice did not return to career menu");
    tap(game,0,4);tap(game);tap(game);tap(game);
    require(game.page()==NativeMenuPage::Race&&game.competitive()&&game.race().lapRecordingStatistics(),"Career league did not automatically attach its recorder");
    game.update(.12,{});require(!game.race().lapGhostPlayback(1),"League playback was enabled");
}
void capturedRace(const std::filesystem::path& chip,const std::filesystem::path& payload,const std::filesystem::path& inputs,
    const std::filesystem::path& reference,const std::filesystem::path& out,Counts& counts){
    auto seed=replay::loadCapture(chip.string().c_str());std::ifstream file(payload,std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(inputs.string().c_str());
    PersonalBestLaps bests;const GhostLapKey key{"captured-trajectory",(*seed)[0x1ca33],bool((*seed)[0x1c9d0])};
    auto scene=std::make_unique<RaceScene>();scene->loadNativeState(*seed,bytes,{});scene->recordPersonalBestLaps(bests,{key,GhostLapSource::Practice});
    auto native=std::make_unique<replay::NativeReplay>(*seed);std::shared_ptr<const GhostLap> last;
    unsigned completions=0;native->presentation=[&](auto stage,const auto&,const auto& effects){
        if(stage!=replay::ReplayPresentationStage::timing)return;
        for(unsigned n=0;n<effects.count;++n)if(effects.events[n].kind==RaceTimingEventKind::lapCounterHud&&effects.events[n].value!=1)++completions;
    };
    for(const auto input:plan){
        advance(*native,input.controls);require(scene->stepLive(input.controls,input.quit),"Complete replay ended early");
        const auto best=bests.best(key);
        if(best!=last){require(best&&best->hundredths==ghostLapHundredths({native->ram[0x57c6b],native->ram[0x57c6c],native->ram[0x57c6d]}),"Recorder ranked an uncorrected or later clock");last=best;}
        for(double at:{0.0,0.25,0.5,0.75,1.0}){
            const auto first=scene->lapGhostPlayback(at);const auto paused=scene->lapGhostPlayback(at);
            require(bool(first)==bool(paused),"Paused query changed target visibility");
            if(first){require(first->pose.position.x==paused->pose.position.x&&first->opacity==paused->opacity,"Paused query advanced target time");++counts.playbackQueries;}
        }
    }
    const auto stats=scene->lapRecordingStatistics();require(stats->completions==completions&&stats->recordedLaps==3&&completions==3&&stats->improvements>=2,"Three original accepted laps were not recorded");
    counts.capturedLaps=completions;require(last&&last->start.interpolated&&last->finish.interpolated,"Earned lap endpoints lacked geometric crossing brackets");
    require(last->hundredths!=unsigned(last->durationMicroseconds/10000),"Game lap time was replaced with wall time");
    // Compare all existing declared trace fields against the identified retained
    // original trace, then full memory against a recording-disabled scene.
    auto verified=std::make_unique<RaceScene>();verified->load(chip,payload,inputs,reference,out/"verified-native.csv");
    if(!scene->liveTerminal())scene->quitLive();if(!verified->liveTerminal())verified->quitLive();
    auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);verified->copyLiveExit(*b);
    require(*a==*b,"Earned recording changed final native state or Hall records");
    require(bests.best(key)==last,"Finish or quit discarded a completed best");
    PersonalBestLaps quitBests;auto quitting=std::make_unique<RaceScene>();quitting->loadNativeState(*seed,bytes,{});
    quitting->recordPersonalBestLaps(quitBests,{key,GhostLapSource::Practice});
    for(const auto input:plan){quitting->stepLive(input.controls,input.quit);if(quitBests.best(key))break;}
    const auto saved=quitBests.best(key);require(bool(saved),"Quit fixture did not complete its first lap");
    quitting->stepLive(1);quitting->quitLive();require(quitBests.best(key)==saved,"Quitting the following incomplete lap discarded the completed best");++counts.quitAfterImprovement;
    // A separately authored competitive fixture retains the same captured
    // human entry pose. Its real native opponent evolves independently; it is
    // not compared against the Practice trace or described as a menu-earned lap.
    auto leagueSeed=std::make_unique<NativeMemory>(*seed);(*leagueSeed)[0x1ca22]=0x80;
    auto opponentData=std::make_unique<OpponentDrivingData>(scr::state::opponent::readData(*leagueSeed));
    auto opponent=scr::state::opponent::readState(*leagueSeed);opponent.segment=std::uint8_t(((*leagueSeed)[0x1ca32]+(*leagueSeed)[0x1ca1a]/2)%(*leagueSeed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;
    initializeOpponentCoordinates(*opponentData,scr::state::opponent::readInputs(*leagueSeed),opponent);scr::state::opponent::writeState(*leagueSeed,opponent);
    auto competitive=std::make_unique<RaceScene>(),unrecorded=std::make_unique<RaceScene>();
    competitive->loadNativeState(*leagueSeed,bytes,{});unrecorded->loadNativeState(*leagueSeed,bytes,{});
    PersonalBestLaps leagueBests;competitive->recordPersonalBestLaps(leagueBests,{key,GhostLapSource::League});
    for(const auto input:plan){
        if(competitive->liveTerminal())break;
        competitive->stepLive(input.controls,input.quit);unrecorded->stepLive(input.controls,input.quit);
        require(!competitive->lapGhostPlayback(1),"Competitive recording displayed its saved target");
    }
    require(competitive->lapRecordingStatistics()->recordedLaps>0,"Competitive fixture did not record an accepted complete lap");
    counts.leagueLaps=competitive->lapRecordingStatistics()->recordedLaps;const auto leagueBest=leagueBests.best(key);
    require(leagueBest&&leagueBest->context.source==GhostLapSource::League,"Competitive lap provenance was lost");
    if(!competitive->liveTerminal())competitive->quitLive();if(!unrecorded->liveTerminal())unrecorded->quitLive();
    competitive->copyLiveExit(*a);unrecorded->copyLiveExit(*b);require(*a==*b,"Competitive recording changed native opponent, Hall or RNG");
    require(leagueBests.best(key)==leagueBest,"Competitive result discarded a completed best");
}
}
int main(int argc,char** argv)try{
    require(argc==3||argc==7,"Usage: native_lap_recording_tests pack output [chip payload controls original-trace]");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);Counts counts;
    RaceScene unloaded;PersonalBestLaps unused;bool quitRejected=false,recordRejected=false;
    try{unloaded.quitLive();}catch(const std::logic_error&){quitRejected=true;}
    try{unloaded.recordPersonalBestLaps(unused,{{"none",0,false},GhostLapSource::Practice});}catch(const std::logic_error&){recordRejected=true;}
    require(quitRejected&&recordRejected,"Unloaded scene did not reject lap/quit operations safely");
    nativeRaces(argv[1],counts);careerBinding(argv[1],output/"player-data");bool captured=false;
    if(argc==7&&std::filesystem::exists(argv[3])&&std::filesystem::exists(argv[4])&&std::filesystem::exists(argv[5])&&std::filesystem::exists(argv[6])){
        capturedRace(argv[3],argv[4],argv[5],argv[6],output,counts);captured=true;
    }
    std::ofstream report(output/"results.json");report<<"{\"status\":\"passed\",\"native_ticks\":"<<counts.nativeTicks<<",\"internal_recovery_timing_updates\":"<<counts.recoveryUpdates
        <<",\"captured_regression_run\":"<<(captured?"true":"false")<<",\"earned_completed_laps\":"<<counts.capturedLaps<<",\"authored_competitive_completed_laps\":"<<counts.leagueLaps
        <<",\"quit_after_improvement\":"<<counts.quitAfterImprovement<<",\"pure_playback_queries\":"<<counts.playbackQueries
        <<",\"full_memory_isolation\":true,\"career_practice_and_league_bound\":true,\"multiplayer_excluded\":true,\"original_cpu_executed\":false}\n";
    std::cout<<"Native lap recording integration passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

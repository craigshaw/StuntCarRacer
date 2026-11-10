#include "race/hall_race_recording.h"
#include "platform/windows/shared_hall_storage.h"
#include "hall_fixture.h"
#include <iostream>

namespace {
using namespace scr;using namespace scr::runtime;using namespace scr::test::hall;
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Drive {
    TrigTable trig{};PersonalBestLaps bests;GhostLapKey key{"career",0,false};
    LapRecorder laps;HallRaceRecording hall;RaceTimingInputs input{};RaceTimingState state{};
    std::int64_t now{};unsigned serial{};bool recovery{};
    std::optional<GhostBridgeShape> bridge;
    Drive(bool competitive=false,unsigned track=0,bool super=false,unsigned player=11,bool privatePb=true)
        :key{"career",track,super},laps(privatePb?LapRecorder(bests,{key,competitive?GhostLapSource::League:GhostLapSource::Practice},trig):LapRecorder(track,trig)),
         hall(id(123),{track,super?HallLeague::Super:HallLeague::Normal,HallRecordKind::Lap},catalog,competitive){
        input.numberOfTrackSegments=64;input.lapLineSegment=1;input.finishLineSegment=32;input.maximumLaps=competitive?4:255;input.currentPlayerId=std::uint8_t(player);
        input.selectedTrackInDivision=competitive?0x80:0;input.segmentIndex[1]=32;input.playerNames.fill(' ');
        const std::string name="  Craig  A  ";std::copy(name.begin(),name.end(),input.playerNames.begin()+16*player);
        state.times.fill({9,0x59,0x99});state.times[0]=state.times[1]=state.times[player+12]={};
        if(track==5){GhostBridgeShape b;b.cachedSections={0x233,900,901};bridge=b;}
    }
    void step(unsigned segment,unsigned progress=255,bool advance=true){
        LapMovementObservation m;m.microseconds=now;m.segment=segment;m.progress=progress;m.segmentLength=1024;m.segmentCount=64;m.lapSegment=1;m.grounded=true;
        m.recovery=recovery;m.restarts=recovery?1:0;m.bridge=bridge;m.pose.worldPosition={100,200,serial++*100};m.pose.averageWheelVelocity=1;
        laps.observeMovement(std::move(m));input.segmentIndex[0]=std::uint8_t(segment);input.trackProgress[0]=std::uint16_t(progress);
        const auto effects=updateRaceTiming(input,state);const auto saved=state;
        state::HumanRaceRecord human;human.player=input.currentPlayerId;
        for(unsigned n=0;n<12;++n)human.name[n]=char(input.playerNames[16*human.player+n]);human.total=state.times[human.player+12];
        laps.observeTiming(effects.humanLapCrossing,[&](LapCompletion c){require(state.times[0].minutes==0&&state.times[0].seconds==0&&state.times[0].subseconds==0,"Timing observer was moved before native clear");hall.complete(std::move(c),human);});
        for(unsigned n=0;n<24;++n)require(state.times[n].minutes==saved.times[n].minutes&&state.times[n].seconds==saved.times[n].seconds&&state.times[n].subseconds==saved.times[n].subseconds,"Delivery changed native timing state");
        require(state.lapCounter==saved.lapCounter,"Delivery changed native lap count");if(advance)now+=120000;
    }
    void start(){step(0,900);step(1,100);}
    void finish(){step(32);step(0,900);step(1);}
};
void modes(){
    for(bool competitive:{false,true})for(bool super:{false,true})for(unsigned player=4;player<=11;++player){
        Drive d(competitive,0,super,player,false);d.start();for(unsigned n=0;n<3;++n)d.finish();
        const auto completed=d.hall.take();require(completed.size()==(competitive?4u:3u),"Lap/race source eligibility differs");
        for(unsigned n=0;n<3;++n){const auto& s=completed[n];
            require(s.attempt.name=="Craig A"&&s.attempt.board.league==(super?HallLeague::Super:HallLeague::Normal)&&s.recording&&s.ghost.empty()&&!s.attempt.ghost,"Driver, league or lazy capture binding differs");
            require(s.attempt.id==hallCompletionId(id(123),n+1,HallRecordKind::Lap),"Attempt identity was not run/crossing/kind bound");
            const auto encoded=makeHallSubmission(s.attempt,s.recording.get());requireHallGhostBinding(decodeHallGhost(encoded.ghost),encoded.attempt,catalog);
            require(encoded.attempt.id!=hallCompletionId(id(124),n+1,HallRecordKind::Lap),"Re-driven run reused attempt ID");
        }
        if(competitive)require(completed.back().attempt.board.kind==HallRecordKind::Race&&hallTimeHundredths(completed.back().attempt.time,HallRecordKind::Race)==hallTimeHundredths(d.state.times[player+12],HallRecordKind::Race)&&!completed.back().recording,"Third-lap total was not native cumulative time");
        require(d.hall.take().empty(),"Draining completions replayed attempts");
    }
}
void pbAndPartial(){
    Drive d;d.start();d.step(32);d.step(1);const auto first=d.bests.best(d.key);require(bool(first),"Fast PB fixture missing");
    d.finish();d.finish();auto completed=d.hall.take();require(completed.size()==3&&d.bests.best(d.key)==first,"Slower/tied laps were filtered by private PB");
    Drive clear;clear.start();clear.step(32);clear.laps.clear();clear.finish();
    auto cleared=clear.hall.take();require(cleared.size()==1&&cleared[0].recording&&!clear.bests.best(clear.key),"Clearing PB interrupted Hall capture or retained partial PB");
    clear.finish();require(bool(clear.bests.best(clear.key)),"PB did not resume next lap");
    Drive partial(true);partial.state.lapCounter[0]=1;partial.state.crossingArmed[0]=0x80;partial.step(1);partial.finish();
    require(partial.hall.take().empty(),"Attaching to a partial lap manufactured a completion");partial.finish();partial.finish();
    const auto later=partial.hall.take();require(later.size()==2&&later.back().attempt.board.kind==HallRecordKind::Lap,"Partial race became a three-lap entry");
    Drive ai;ai.start();ai.input.segmentIndex[1]=1;ai.step(32);require(ai.hall.take().empty(),"Opponent crossing created a human entry");
}
void overflowAndRecovery(){
    Drive samples;samples.start();for(unsigned n=0;n<=maxGhostLapSamples;++n)samples.step(32);samples.finish();
    auto full=samples.hall.take();require(full.size()==1&&!full[0].recording,"Sample overflow lost time-only eligibility");
    samples.finish();require(samples.hall.take().front().recording!=nullptr,"Sample overflow did not resume capture");
    Drive shapes(false,5);shapes.start();for(unsigned n=0;n<=maxGhostLapBridgeShapes;++n){shapes.bridge->nearGeometry[0]=std::uint8_t(n+1);shapes.step(32);}shapes.finish();
    full=shapes.hall.take();require(full.size()==1&&!full[0].recording,"Bridge overflow lost valid time");
    Drive repeated;repeated.start();repeated.recovery=true;repeated.step(32,255,false);repeated.step(1,255,false);repeated.step(32,255,false);repeated.step(1,255,false);
    full=repeated.hall.take();require(full.size()==2&&full[0].attempt.id!=full[1].attempt.id,"Same-timestamp timing calls reused attempt identity");
    require(full[0].recording&&!full[1].recording,"Zero movement span was encoded as a replayable lap");
}
}
int main()try{modes();pbAndPartial();overflowAndRecovery();std::cout<<"Hall completion stream: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

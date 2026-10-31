#include "race/lap_recording.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr;using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Drive {
    TrigTable trig{};LapRecorder recorder;RaceTimingInputs input;RaceTimingState state;
    std::int64_t time{};unsigned restarts{},serial{};bool recovery{};
    std::optional<GhostBridgeShape> bridge;
    Drive(PersonalBestLaps& bests,GhostLapContext context):recorder(bests,std::move(context),trig){
        input.numberOfTrackSegments=64;input.lapLineSegment=1;input.finishLineSegment=32;input.maximumLaps=255;input.currentPlayerId=11;
        input.selectedTrackInDivision=recorder.context().source==GhostLapSource::League?0x80:0;input.segmentIndex[1]=32;
        state.times.fill({9,0x59,0x99});state.times[0]=state.times[1]={};
    }
    void step(unsigned segment,unsigned progress=0,DriveControlPreset preset=DriveControlPreset::Original,bool advance=true){
        LapMovementObservation movement;movement.microseconds=time;movement.segment=segment;movement.progress=progress;
        movement.segmentLength=1024;movement.segmentCount=64;movement.lapSegment=1;movement.grounded=true;
        movement.restarts=restarts;movement.recovery=recovery;movement.preset=preset;
        movement.bridge=bridge;
        movement.pose.worldPosition={std::uint32_t((100+serial*10)<<16),40u<<16,512u<<16};++serial;
        movement.pose.averageWheelVelocity=1;recorder.observeMovement(movement);
        input.segmentIndex[0]=std::uint8_t(segment);input.trackProgress[0]=std::uint16_t(progress);
        recorder.observeTiming(updateRaceTiming(input,state));if(advance)time+=120000;
    }
    void start(){step(0,900);step(1,100);}
    void finish(unsigned progress=255){step(32);step(0,900);step(1,progress);}
};
const GhostLapKey key{"career-a",0,false};
void rankingAndTargets(){
    PersonalBestLaps bests;Drive practice(bests,{key,GhostLapSource::Practice});practice.start();practice.finish(32);
    auto first=bests.best(key);require(bool(first),"First completed lap was not retained");
    require(first->hundredths==50&&first->finish.correctionHundredths==11,"Original decimal crossing correction was lost");
    require(first->start.interpolated&&first->finish.interpolated,"Adjacent crossing neighbours were not aligned");
    require(first->durationMicroseconds!=500000&&first->samples.front().microseconds<0&&first->samples.back().microseconds>first->durationMicroseconds,
        "Movement clock was derived from lap digits or crossing brackets were discarded");
    require(!practice.recorder.playback(first->finish.alignedMicroseconds-1),"New best appeared before the displayed crossing");
    auto target=practice.recorder.playback(first->finish.alignedMicroseconds);
    require(target&&target->target==first,"Improvement was not selected for the next attempt");
    Drive league(bests,{key,GhostLapSource::League});league.start();league.finish();auto second=bests.best(key);
    require(second!=first&&second->hundredths==39&&second->context.source==GhostLapSource::League,"League did not replace Practice best");
    require(!league.recorder.playback(league.time),"League displayed a target");
    require(practice.recorder.playback(first->finish.alignedMicroseconds+120000)->target==first,"Another result changed a target midway through its attempt");
    Drive faster(bests,{key,GhostLapSource::Practice});faster.start();faster.step(32);faster.step(1,255,DriveControlPreset::Modern);
    auto third=bests.best(key);require(third!=second&&third->hundredths==26&&third->controlPresets==3,"Practice did not replace league best or control metadata was lost");
    Drive tie(bests,{key,GhostLapSource::Practice});tie.start();tie.step(32);tie.step(1,255);
    require(bests.best(key)==third,"Equal time replaced the existing target");
    const auto start=third->finish.alignedMicroseconds;
    auto fading=faster.recorder.playback(start+third->durationMicroseconds+120000);
    require(fading&&fading->finished&&std::abs(fading->opacity-0.5)<1e-9,"Ghost did not fade after its recorded finish");
    require(faster.recorder.playback(start+third->durationMicroseconds+240000)->opacity==0,"Finished ghost looped or waited at the line");
    for(unsigned n=0;n<20;++n){auto again=faster.recorder.playback(start+third->durationMicroseconds+120000);require(again->opacity==fading->opacity&&again->pose.position.x==fading->pose.position.x,"Paused/reverse query advanced playback");}
    faster.recorder.stop();require(bests.best(key)==third,"Quit discarded a completed improvement");
    require(!bests.best({"career-b",0,false})&&!bests.best({"career-a",1,false})&&!bests.best({"career-a",0,true}),"Career, circuit or league isolation failed");
    bool rejected=false;try{ghostLapHundredths({0,0x60,0});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid original time was accepted");
}
void partialRecoveryAndBounds(){
    PersonalBestLaps bests;Drive partial(bests,{key,GhostLapSource::Practice});
    partial.state.lapCounter[0]=1;partial.state.crossingArmed[0]=0x80;partial.step(1);partial.finish();
    require(!bests.best(key),"Partial race-entry lap replaced a target");partial.finish();require(bool(bests.best(key)),"Recorder did not resynchronize at the next boundary");
    Drive recovery(bests,{{"recovery",0,false},GhostLapSource::Practice});recovery.start();recovery.step(32);
    recovery.restarts=1;recovery.recovery=true;recovery.step(32,0,DriveControlPreset::Original,false);recovery.step(32,0,DriveControlPreset::Original,false);
    recovery.recovery=false;recovery.step(32);recovery.finish();auto lap=bests.best({"recovery",0,false});
    require(lap&&recovery.recorder.statistics().recoveryTimingUpdates==2&&lap->hundredths==91,"Recovery's internal timing updates were omitted or invalidated the lap");
    const auto cut=std::find_if(lap->samples.begin(),lap->samples.end(),[](const auto& s){return s.cut;});
    require(cut!=lap->samples.end()&&cut!=lap->samples.begin(),"Recovery discontinuity was not recorded");
    const auto query=lap->finish.alignedMicroseconds+cut->microseconds-1;
    const auto before=recovery.recorder.playback(query);
    require(before&&std::abs(before->pose.position.x-makeGhostCarPose((cut-1)->pose).position.x)<1e-8,"Playback blended through a recovery cut");
    const auto existing=bests.best(key);Drive overflow(bests,{key,GhostLapSource::Practice});overflow.start();
    for(unsigned n=0;n<maxGhostLapSamples+1;++n)overflow.step(32);
    overflow.finish();require(overflow.recorder.statistics().overflowCandidates==1&&bests.best(key)==existing,"Bounded overflow replaced or erased a good best");
    overflow.finish();require(overflow.recorder.statistics().starts>=3,"Overflow did not resume at the next lap");
    bests.forgetCareer("career-a");require(!bests.best(key)&&bests.best({"recovery",0,false}),"Career removal affected another owner's targets");
}
void bridgePlayback(){
    PersonalBestLaps bests;const GhostLapKey bridgeKey{"bridge",5,false};Drive drive(bests,{bridgeKey,GhostLapSource::Practice});
    GhostBridgeShape shape;shape.cachedSections={0x33,1200,1200};drive.bridge=shape;
    drive.start();drive.step(49);drive.step(51);drive.step(55);drive.step(56);drive.step(56);drive.finish();
    const auto lap=bests.best(bridgeKey);require(lap&&lap->bridges.size()==1,"Identical bridge shapes were not compacted");
    shape.nearGeometry[0]=1;drive.bridge=shape;
    bool suppressed=false,returned=false,finishedGuard=false;
    for(unsigned n=0;n<14;++n){
        drive.step(32);const auto result=drive.recorder.playback(drive.time-120000);
        require(bool(result),"Bridge target disappeared from its fixed attempt");
        suppressed|=result->conditionsDiffer&&result->opacity==0;
        finishedGuard|=suppressed&&!result->conditionsDiffer;
        returned|=finishedGuard&&result->opacity>0;
        const auto repeat=drive.recorder.playback(drive.time-120000);
        require(repeat->opacity==result->opacity,"Bridge visibility advanced during rendering");
    }
    require(suppressed&&returned,"Recorded bridge guard did not suppress and return locally");
    const auto saved=bests.best(bridgeKey);Drive bounded(bests,{bridgeKey,GhostLapSource::Practice});bounded.bridge=shape;bounded.start();
    for(unsigned n=0;n<maxGhostLapBridgeShapes+1;++n){shape.nearGeometry[1]=std::uint8_t(n);bounded.bridge=shape;bounded.step(32);}
    bounded.finish();require(bounded.recorder.statistics().overflowCandidates==1&&bests.best(bridgeKey)==saved,"Bridge shape overflow damaged an existing target");
}
}
int main()try{rankingAndTargets();partialRecoveryAndBounds();bridgePlayback();std::cout<<"Lap recording timing, ranking, target, recovery and bounds checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

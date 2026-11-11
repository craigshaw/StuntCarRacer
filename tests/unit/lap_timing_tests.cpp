#include "race/lap_timing.h"
#include "race/lap_recording.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace scr;using namespace scr::runtime;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
timing::Gate gate(){return {2,0,1,0,44,1,5*timing::cellSize,3*timing::cellSize,true};}
struct Drive {
    LapTiming clock;
    RaceTimingInputs input;
    RaceTimingState original;
    std::int64_t now{};
    unsigned restarts{};
    bool recovery{},found{true};unsigned player=11;
    std::optional<HumanLapCrossing> lastEvent;
    std::vector<LapTimingCompletion> completed;
    TrigTable trig{};PersonalBestLaps bests;std::unique_ptr<LapRecorder> recorder;
    std::vector<LapCompletion> recorded;
    explicit Drive(bool competitive=false):clock(gate(),competitive){
        input.currentPlayerId=11;input.numberOfTrackSegments=44;input.lapLineSegment=1;input.finishLineSegment=22;
        input.maximumLaps=competitive?4:255;input.selectedTrackInDivision=competitive?0x80:0;input.segmentIndex[1]=22;
        original.times.fill({9,0x59,0x99});original.times[0]=original.times[1]=original.times[23]={};
    }
    void step(unsigned segment,std::int64_t distance=-64,bool advance=true){
        timing::Observation o;o.us=now;o.segment=segment;o.restarts=restarts;o.recovery=recovery;o.found=found;o.player=player;
        o.position={std::uint32_t(3*timing::cellSize+timing::cellSize/2),0,std::uint32_t(5*timing::cellSize+distance)};
        o.progress=segment==1?unsigned(std::max(std::int64_t(0),distance)/4096):2047;
        clock.observeMovement(o);
        if(recorder){LapMovementObservation m;m.microseconds=o.us;m.pose.worldPosition=o.position;m.segment=segment;
            m.progress=o.progress;m.segmentLength=2048;m.segmentCount=44;m.lapSegment=1;m.restarts=restarts;m.recovery=recovery;
            recorder->observeMovement(m);}
        input.segmentIndex[0]=std::uint8_t(segment);input.trackProgress[0]=std::uint16_t(o.progress);
        lastEvent=updateRaceTiming(input,original).humanLapCrossing;
        auto result=clock.observeTiming(lastEvent,original.times[23]);
        if(recorder)recorder->observeTiming(lastEvent,[&](LapCompletion lap){recorded.push_back(std::move(lap));},result?&result->enhanced:nullptr);
        if(result)completed.push_back(*result);
        if(advance)now+=timing::tickUs;
    }
    void start(){step(0,-60000);step(1,60000);}
    void finish(std::int64_t offset=60000){step(22);step(0,-offset);step(1,120000-offset);}
    void recover(){++restarts;recovery=true;step(22,-64,false);step(22,-64,false);recovery=false;}
};
void ordinaryAndRounding(){
    Drive d(true);d.start();d.finish(60499);d.finish(60998);d.finish(61497);
    check(d.completed.size()==3,"Three full laps were not captured");
    std::int64_t sum=0,rounded=0;
    for(const auto& lap:d.completed){
        check(lap.enhanced.lap.rankable()&&lap.enhanced.lap.elapsedMicroseconds==360499,"Fractional start/finish elapsed time differs");
        check(lap.enhanced.lap.methodVersion==1&&lap.enhanced.lap.penaltyMicroseconds==0,"Timing identity or penalty differs");
        sum+=lap.enhanced.lap.scoredMicroseconds;rounded+=lap.enhanced.lap.milliseconds();
    }
    const auto& race=d.completed.back().enhanced.race;check(bool(race),"Competitive three-lap total is absent");
    check(race->enhanced.scoredMicroseconds==sum&&race->enhanced.milliseconds()==1081&&rounded==1080,"Race rounded individual laps before accumulation");
    check(race->original.seconds==d.original.times[23].seconds&&race->original.subseconds==d.original.times[23].subseconds,"Original cumulative snapshot differs");
    check(!d.clock.observeTiming(d.lastEvent,d.original.times[23]),"Duplicate event emitted a second completion");
    Drive practice;practice.start();practice.finish();practice.finish();practice.finish();
    check(!practice.completed.back().enhanced.race,"Practice minted a competitive race result");
}
void recoveryAndConfidence(){
    Drive d;d.start();d.recover();d.finish();
    const auto& time=d.completed.back().enhanced.lap;
    check(time.rankable()&&time.elapsedMicroseconds==360000&&time.recoveries==1&&time.penaltyMicroseconds==260000&&time.scoredMicroseconds==620000,"Recovery elapsed time or explicit penalty differs");
    check(d.clock.statistics().recoveries==1&&d.clock.statistics().recoveryObservations==2,"Recovery callbacks were double-charged");
    check(d.completed.back().original.subseconds==0x77,"Original extra timing updates were changed");
    Drive before;before.recover();before.start();before.finish();
    check(before.completed.back().enhanced.lap.recoveries==0,"Recovery before the observed start charged the next lap");
    Drive uncertain(true);uncertain.step(1,60000);uncertain.finish();
    check(uncertain.completed.back().enhanced.lap.hasEstimate&&!uncertain.completed.back().enhanced.lap.rankable(),"Missing start bracket lost its approximate result or became rankable");
    uncertain.finish();check(uncertain.completed.back().enhanced.lap.rankable(),"Valid later endpoints did not recover eligibility");
    uncertain.finish();check(uncertain.completed.back().enhanced.race&&!uncertain.completed.back().enhanced.race->enhanced.rankable(),"Uncertain constituent lap became a ranked race");
    Drive middle(true);middle.start();middle.step(22);middle.found=false;middle.step(0,-60000);middle.found=true;middle.step(1,60000);middle.finish();middle.finish();
    check(middle.completed.back().enhanced.race&&!middle.completed.back().enhanced.race->enhanced.rankable()&&middle.completed.back().enhanced.lap.rankable(),"Reliable race endpoints hid an uncertain intermediate boundary");
    Drive lookup;lookup.start();lookup.step(22);lookup.found=false;lookup.step(0,-60000);lookup.found=true;lookup.step(1,60000);
    check(!lookup.completed.back().enhanced.lap.rankable(),"Stale start-side lookup became rankable");
    Drive changed;changed.start();changed.player=10;changed.finish();
    check(changed.completed.back().enhanced.lap.confidence==timing::Reason::player,"Player identity change was accepted");
    Drive reset;reset.start();reset.recover();reset.restarts=0;reset.finish();
    check(reset.completed.back().enhanced.lap.confidence==timing::Reason::penaltySequence,"Recovery counter reset retained ranking eligibility");
    reset.finish();check(reset.completed.back().enhanced.lap.rankable(),"Known recovery sequence did not resynchronise");
    Drive placement;placement.start();placement.step(22);++placement.restarts;placement.recovery=true;
    placement.step(0,-60000,false);placement.step(1,60000,false);placement.recovery=false;
    check(!placement.completed.back().enhanced.lap.rankable()&&placement.completed.back().enhanced.lap.recoveries==1,"Recovery placement was interpolated or its penalty was lost");
    placement.finish();check(!placement.completed.back().enhanced.lap.rankable(),"Uncertain recovery start became rankable");
    placement.finish();check(placement.completed.back().enhanced.lap.rankable()&&placement.completed.back().enhanced.lap.recoveries==0,"Recovery boundary did not resynchronise without double charge");
}
void partialAndStop(){
    Drive partial(true);partial.original.lapCounter[0]=1;partial.finish();
    check(partial.completed.empty(),"Attaching mid-lap manufactured a completion");partial.finish();partial.finish();
    check(partial.completed.size()==2&&!partial.completed.back().enhanced.race,"Partial race became a three-lap result");
    Drive gap(true);gap.start();gap.original.lapCounter[0]=2;gap.finish();
    check(gap.completed.empty(),"Missing native counter was treated as a full lap");gap.finish();
    check(gap.completed.size()==1&&!gap.completed.back().enhanced.race,"Gap did not resynchronise conservatively");
    Drive stopped;stopped.start();stopped.clock.stop();stopped.finish();
    check(stopped.completed.empty()&&stopped.clock.statistics().completions==0,"Quit produced a partial completion");
}
void arithmeticAndBounds(){
    LapTimingBoundary a,b;a.confidence=b.confidence=timing::Reason::reliable;b.crossingMicroseconds=500;
    check(measureEnhancedTime(a,b).milliseconds()==1,"Half millisecond did not round upward");
    b.crossingMicroseconds=499;check(measureEnhancedTime(a,b).milliseconds()==0,"Sub-half millisecond rounded upward");
    b.crossingMicroseconds=std::numeric_limits<std::int64_t>::max();
    check(measureEnhancedTime(a,b).rankable()&&measureEnhancedTime(a,b).milliseconds()>0,"Large valid time overflowed during rounding");
    b.recoveries=1;check(measureEnhancedTime(a,b).confidence==timing::Reason::arithmetic,"Scored time overflow was accepted");
    b.crossingMicroseconds=1;b.recoveries=std::numeric_limits<std::uint64_t>::max();
    check(measureEnhancedTime(a,b).confidence==timing::Reason::arithmetic,"Penalty multiplication overflow was accepted");
    b=a;check(!measureEnhancedTime(a,b).rankable(),"Zero movement time was ranked");
    TrackProgressData malformed;malformed.track.segmentCount=44;check(!timing::gate(malformed,1).valid,"Malformed geometry escaped unsupported classification");
}
void recordingOverflow(){
    Drive d;d.recorder=std::make_unique<LapRecorder>(d.bests,GhostLapContext{{"timing",0,false},GhostLapSource::Practice},d.trig);
    d.start();for(unsigned n=0;n<=maxGhostLapSamples;++n)d.step(22);d.finish();
    check(d.completed.size()==1&&d.completed.back().enhanced.lap.rankable(),"Recording overflow invalidated independent timing");
    check(d.recorded.size()==1&&!d.recorded.back().recording&&d.recorded.back().enhanced&&d.recorded.back().enhanced->lap.rankable(),"Time-only completion lost dual metadata");
    check(d.recorded.back().time.subseconds==d.completed.back().original.subseconds,"Dual metadata changed Original completion");
}
}
int main()try{ordinaryAndRounding();recoveryAndConfidence();partialAndStop();arithmeticAndBounds();recordingOverflow();std::cout<<"Lap timing: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

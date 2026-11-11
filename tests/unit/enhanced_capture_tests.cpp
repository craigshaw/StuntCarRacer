#include "race/hall_race_recording.h"
#include "enhanced_record_fixture.h"
#include <iostream>

namespace {
using namespace scr;using namespace scr::runtime;namespace fixture=scr::test::enhanced;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Drive {
    TrigTable trig{};PersonalBestLaps original;EnhancedPersonalBests enhanced{fixture::catalog};
    GhostLapKey key{std::string(32,'1'),0,false};LapRecorder laps;HallRaceRecording hall;
    RaceTimingInputs input{};RaceTimingState state{};std::int64_t now{};unsigned restarts{},serial{};
    bool recovery{},uncertain{};std::optional<GhostBridgeShape> bridge;
    std::optional<LapTimingBoundary> start,raceStart;
    Drive(bool competitive=false,unsigned track=0,std::shared_ptr<const PinnedEnhancedHallGhost> rival={})
        :key{std::string(32,'1'),track,false},laps(original,{key,competitive?GhostLapSource::League:GhostLapSource::Practice,fixture::catalog.trackContent},trig),
         hall(fixture::id(123),{track,HallLeague::Normal,HallRecordKind::Lap},{fixture::catalog.trackContent,1},competitive){
        laps.configureEnhanced(enhanced,{key.career,track,false},std::move(rival));
        input.numberOfTrackSegments=64;input.lapLineSegment=1;input.finishLineSegment=32;input.maximumLaps=competitive?4:255;
        input.currentPlayerId=11;input.selectedTrackInDivision=competitive?0x80:0;input.segmentIndex[1]=32;
        state.times.fill({9,0x59,0x99});state.times[0]=state.times[1]=state.times[23]={};
        if(track==5){GhostBridgeShape b;b.cachedSections={0x33,900,901};bridge=b;}
    }
    void step(unsigned segment,unsigned progress=100,bool advance=true){
        LapMovementObservation m;m.microseconds=now;m.segment=segment;m.progress=progress;m.segmentLength=1024;m.segmentCount=64;m.lapSegment=1;
        m.grounded=true;m.restarts=restarts;m.recovery=recovery;m.bridge=bridge;m.pose.worldPosition={serial++*100u,200,300};m.pose.averageWheelVelocity=1;
        laps.observeMovement(m);input.segmentIndex[0]=std::uint8_t(segment);input.trackProgress[0]=std::uint16_t(progress);
        const auto crossing=updateRaceTiming(input,state).humanLapCrossing;
        std::optional<EnhancedLapTiming> timing;LapTimingBoundary line;
        if(crossing){line.observedMicroseconds=now;line.crossingMicroseconds=now-60000;line.recoveries=restarts;line.player=11;
            line.confidence=uncertain?timing::Reason::missingPrevious:timing::Reason::reliable;
            if(start&&crossing->completedTime){timing.emplace();timing->start=*start;timing->finish=line;timing->lap=measureEnhancedTime(*start,line);
                if(input.selectedTrackInDivision&&crossing->counter==4&&raceStart)timing->race=TimedRaceResult{state.times[23],measureEnhancedTime(*raceStart,line)};}
            if(!raceStart)raceStart=line;start=line;
        }
        state::HumanRaceRecord human;human.player=11;human.name.fill(' ');human.name[0]='A';human.total=state.times[23];
        laps.observeTiming(crossing,[&](LapCompletion c){hall.complete(std::move(c),human);},timing?&*timing:nullptr,crossing?&line:nullptr);
        if(advance)now+=120000;
    }
    void begin(){step(0,900);step(1);}
    void finish(){step(32);step(0,900);step(1);}
    EnhancedBestKey enhancedKey()const{return {key.career,key.track,key.superLeague};}
    std::optional<GhostLapPlayback> playback(std::int64_t at)const{return laps.playback(at,true,TimingCategory::Enhanced);}
};
void captures(){
    Drive d(true);d.begin();d.finish();d.finish();d.finish();
    auto original=d.hall.take();auto enhanced=d.hall.takeEnhanced();check(original.size()==4&&enhanced.size()==4,"Dual lap/race delivery differs");
    for(unsigned n=0;n<3;++n){const auto& a=original[n];const auto& b=enhanced[n];
        check(a.attempt.id!=b.attempt.id&&b.attempt.id==enhancedCompletionId(fixture::id(123),n+1,HallRecordKind::Lap),"Timing category identity collided");
        check(a.recording&&b.recording&&a.recording->start.alignedMicroseconds!=b.recording->recording.start.crossingMicroseconds,"Enhanced reused progress-aligned boundaries");
        const auto encoded=makeEnhancedSubmission(b.attempt,b.recording.get());const auto g=decodeEnhancedHallGhost(encoded.ghost);validateEnhancedRecording(g.recording,0);
        check(g.recording.time.elapsedMicroseconds==360000&&g.recording.samples.front().microseconds==-60000,"Raw movement samples were not rebased to world-plane crossings");
    }
    check(enhanced.back().attempt.time.scoredMicroseconds==1080000&&!enhanced.back().recording,"Enhanced race accumulated rounded lap scores or acquired poses");
    check(d.hall.takeEnhanced().empty()&&d.hall.take().empty(),"Draining dual delivery replayed attempts");
    check(!d.playback(d.now-120000),"Competitive capture displayed a Practice target");
}
void independentPbsAndClear(){
    Drive d;GhostLap old;old.context={d.key,GhostLapSource::Practice,fixture::catalog.trackContent};old.hundredths=1;d.original.restore(std::move(old));
    d.begin();d.finish();check(d.original.best(d.key)->hundredths==1&&d.enhanced.best(d.enhancedKey()),"Enhanced PB depended on Original improvement");
    const auto first=d.enhanced.best(d.enhancedKey());auto target=d.playback(d.start->crossingMicroseconds+1);check(target&&target->enhancedTarget==first->recording,"Enhanced target was not selected at the next boundary");
    d.laps.clearEnhanced();check(!d.enhanced.best(d.enhancedKey())&&d.original.best(d.key)&&!d.playback(d.now),"Enhanced clear changed the Original PB or kept its target");
    d.finish();check(!d.enhanced.best(d.enhancedKey())&&d.hall.takeEnhanced().size()==2,"Enhanced clear restored a partial PB or stopped Hall capture");
    d.finish();check(bool(d.enhanced.best(d.enhancedKey())),"Enhanced PB did not resume after the next boundary");
    const auto held=d.enhanced.best(d.enhancedKey());d.laps.clear();check(d.enhanced.best(d.enhancedKey())==held&&!d.original.best(d.key),"Original clear changed Enhanced PB");
    Drive reverse;auto fast=fixture::best(1,false);reverse.enhanced.offer(fast);reverse.begin();reverse.finish();
    check(reverse.original.best(reverse.key)&&reverse.enhanced.best(reverse.enhancedKey())->time.elapsedMicroseconds==1,"Original PB depended on Enhanced improvement");
}
void recoveryAndPinned(){
    Drive d;d.begin();++d.restarts;d.recovery=true;d.step(32,100,false);d.step(32,100,false);d.recovery=false;d.now+=120000;d.finish();
    const auto best=d.enhanced.best(d.enhancedKey());check(best&&best->recording&&best->time.recoveries==1&&best->time.penaltyMicroseconds==260000,"Recovery capture lost or duplicated the penalty");
    check(best->recording->recoveryOffsets.size()==1,"Recovery capture lost its movement cut");
    const auto end=d.playback(d.start->crossingMicroseconds+best->time.elapsedMicroseconds+1000);
    check(end&&end->finished&&end->elapsedMicroseconds<best->time.scoredMicroseconds,"Penalty delayed ghost movement or finish");
    auto a=fixture::attempt(9,best->time.elapsedMicroseconds);a.time=best->time;EnhancedHallGhost g{fixture::catalog,a.id,a.board,*best->recording};
    const auto pinned=pinEnhancedHallGhost(a,g,fixture::catalog);Drive rival(false,0,pinned);rival.begin();
    const auto query=rival.start->crossingMicroseconds+1;check(rival.playback(query)->enhancedHallTarget==pinned,"Enhanced rival did not start on the first boundary");
    rival.finish();rival.laps.clearEnhanced();check(rival.playback(rival.now)->enhancedHallTarget==pinned,"PB improvement or clear replaced the pinned rival");
    rival.enhanced.forgetCareer(rival.key.career);
    for(const auto at:{rival.now,query,rival.now-120000,query})check(rival.playback(at)->enhancedHallTarget==pinned,"Repeated/reverse queries or PB removal changed the rival");
}
void uncertainAndOverflow(){
    Drive d;d.uncertain=true;d.begin();d.uncertain=false;d.finish();check(d.hall.take().size()==1&&d.hall.takeEnhanced().empty()&&!d.enhanced.best(d.enhancedKey()),"Uncertain Enhanced estimate reached rankings or blocked Original");
    d.finish();check(d.hall.takeEnhanced().size()==1&&d.enhanced.best(d.enhancedKey()),"Reliable timing did not resynchronise");
    Drive samples;samples.begin();for(unsigned n=0;n<=maxGhostLapSamples;++n)samples.step(32);samples.finish();
    auto best=samples.enhanced.best(samples.enhancedKey());auto rows=samples.hall.takeEnhanced();check(best&&!best->recording&&rows.size()==1&&!rows[0].recording,"Sample overflow discarded a reliable Enhanced time");
    samples.finish();check(samples.enhanced.best(samples.enhancedKey())->recording&&samples.hall.takeEnhanced().front().recording,"Capture did not resume after sample overflow");
    Drive shapes(false,5);shapes.begin();for(unsigned n=0;n<=maxGhostLapBridgeShapes;++n){shapes.bridge->nearGeometry[0]=std::uint8_t(n+1);shapes.step(32);}shapes.finish();
    check(shapes.enhanced.best(shapes.enhancedKey())&&!shapes.enhanced.best(shapes.enhancedKey())->recording&&shapes.hall.takeEnhanced().size()==1,"Bridge overflow discarded Enhanced timing");
}
void bridgeGuard(){
    Drive d(false,5);d.begin();for(unsigned segment:{49u,51u,55u,56u,56u})d.step(segment);d.finish();
    const auto best=d.enhanced.best(d.enhancedKey());check(best&&best->recording,"Enhanced bridge recording missing");d.bridge->nearGeometry[0]=1;
    bool hidden=false,returned=false,cleared=false;
    for(unsigned n=0;n<14;++n){d.step(32);const auto at=d.now-120000;const auto before=d.playback(at);check(bool(before),"Enhanced bridge target disappeared");
        hidden|=before->conditionsDiffer&&before->opacity==0;cleared|=hidden&&!before->conditionsDiffer;returned|=cleared&&before->opacity>0;
        check(d.playback(at)->opacity==before->opacity,"Enhanced bridge visibility changed during a query");}
    check(hidden&&returned,"Enhanced bridge guard did not suppress and restore locally");
}
}
int main()try{captures();independentPbsAndClear();recoveryAndPinned();uncertainAndOverflow();bridgeGuard();std::cout<<"Enhanced capture: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

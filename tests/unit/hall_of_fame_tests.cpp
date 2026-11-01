#include "hall_fixture.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace scr;using namespace scr::runtime;namespace fixture=scr::test::hall;
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F call){bool caught=false;try{call();}catch(const std::exception&){caught=true;}require(caught,"Invalid Hall value was accepted");}
}
int main()try{
    require(normaliseHallName("  CrAiG  ").display=="CrAiG","Display capitalisation was lost");
    require(normaliseHallName("A  B ").identity==normaliseHallName("a b").identity,"Name spaces/case split a competitor");
    require(normaliseHallName("A./:09").identity=="a./:09","Native name punctuation was rejected");
    for(auto name:{"","            ","1234567890123","A-B","A_B","A\tB","A\nB"})rejected([&]{normaliseHallName(name);});
    for(unsigned n=0;n<60000;++n)require(hallTimeHundredths(fixture::time(n),HallRecordKind::Lap)==n,"Legal lap BCD did not roundtrip");
    for(unsigned n=0;n<=179997;++n)require(hallTimeHundredths(fixture::time(n),HallRecordKind::Race)==n,"Legal cumulative BCD did not roundtrip");
    for(auto t:{PackedLapTime{0,0x60,0},PackedLapTime{0x0a,0,0},PackedLapTime{0,0,0xfa},fixture::time(60000)})rejected([&]{hallTimeHundredths(t,HallRecordKind::Lap);});
    rejected([&]{hallTimeHundredths(fixture::time(179998),HallRecordKind::Race);});
    require(formatHallTime(fixture::time(59999),HallRecordKind::Lap)=="9:59.99"&&formatHallTime(fixture::time(179997),HallRecordKind::Race)=="29:59.97","Long valid times became empty sentinels");
    // Compare the boundary with the real native cumulative routine, not a
    // second version of our BCD conversion.
    RaceTimingState timing;RaceTimingInputs in;in.currentPlayerId=11;in.lapLineSegment=1;in.finishLineSegment=2;in.segmentIndex[0]=1;in.trackProgress[0]=0x100;in.maximumLaps=4;in.numberOfTrackSegments=8;timing.lapCounter[0]=1;
    for(unsigned n=0;n<3;++n){timing.times[0]={9,0x59,0x86};timing.crossingArmed[0]=0;updateRaceTiming(in,timing);}
    const auto total=timing.times[23];require(hallTimeHundredths(total,HallRecordKind::Race)==179997,"Native three-lap maximum differs from Hall validation");
    HallOfFame hall(fixture::catalog);
    for(unsigned b=0;b<hallBoardCount;++b){const auto key=hallBoardKey(b);require(hallBoardIndex(key)==b,"Hall board mapping collided");
        for(unsigned n=0;n<20;++n)require(hall.offer(fixture::attempt(1+b*20+n,1000+n,key))==HallAdmission::Accepted,"A qualifying same-name attempt was lost");
        require(hall.entries(key).size()==20,"Board capacity is not 20");
        require(hall.offer(fixture::attempt(10000+b,1019,key))==HallAdmission::OutsideTable,"A cutoff tie displaced an existing entry");
        require(hall.offer(fixture::attempt(11000+b,9999,key))==HallAdmission::OutsideTable,"A slower attempt entered a full table");
        const auto old=hall.entries(key).front().attempt;
        require(hall.offer(old)==HallAdmission::Duplicate,"Retry duplicated an attempt");
        auto conflict=old;conflict.name="Alice";rejected([&]{hall.offer(conflict);});
        const auto evicted=hall.entries(key).back().attempt;
        require(hall.offer(fixture::attempt(12000+b,500,key))==HallAdmission::Accepted,"A faster attempt failed to displace twentieth");
        require(hall.entries(key).size()==20&&hall.rank(key,0)==1,"Faster admission damaged table bounds/rank");
        require(hall.offer(evicted)==HallAdmission::OutsideTable,"A retry of an evicted attempt re-entered a monotonically faster table");
    }
    HallOfFame ties(fixture::catalog);
    for(unsigned n=0;n<12;++n)ties.offer(fixture::attempt(20000+n,n<11?1000:1100));
    require(ties.rank({},0)==1&&ties.rank({},10)==1&&ties.rank({},11)==12,"Competition ranks broke across the page boundary");
    for(unsigned n=0;n<11;++n)require(ties.entries({})[n].attempt.id==fixture::id(20000+n),"Existing tie order changed");
    require(ties.offer(fixture::attempt(21000,1000,{},"CRAIG"))==HallAdmission::Accepted,"Same-name/time genuine attempt was deduplicated");
    auto recording=fixture::attempt(22000);recording.ghost=HallGhostDigest{};(*recording.ghost)[0]=1;
    require(ties.offer(recording)==HallAdmission::Accepted,"Lap recording reference was not retained");
    recording.id=fixture::id(22001);recording.board.kind=HallRecordKind::Race;rejected([&]{ties.offer(recording);});
    auto bad=fixture::attempt(23000);bad.id={};rejected([&]{ties.offer(bad);});
    bad=fixture::attempt(23000);bad.board.track=8;rejected([&]{ties.offer(bad);});
    bad=fixture::attempt(23000);bad.board.league=HallLeague(2);rejected([&]{ties.offer(bad);});
    rejected([&]{hallBoardKey(32);});rejected([&]{ties.rank({},99);});
    auto state=ties.snapshot();state.nextInsertionOrder=0;rejected([&]{HallOfFame invalid(state);});
    state=ties.snapshot();state.boards[0].push_back(state.boards[0][0]);rejected([&]{HallOfFame invalid(state);});
    state=ties.snapshot();std::swap(state.boards[0][0],state.boards[0][1]);rejected([&]{HallOfFame invalid(state);});
    state=ties.snapshot();state.nextInsertionOrder=std::numeric_limits<std::uint64_t>::max();HallOfFame exhausted(state);rejected([&]{exhausted.offer(fixture::attempt(24000,1));});
    require(ties.entries({}).size()>1,"Multiple attempts were reduced to one PB");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"boards\":32,\"entry_limit\":20,\"native_cumulative_time_checked\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

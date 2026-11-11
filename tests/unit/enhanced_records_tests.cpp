#include "enhanced_record_fixture.h"
#include "persistence/ghost_lap_codec.h"
#include "persistence/hall_table_codec.h"
#include <iostream>
#include <limits>

namespace {
using namespace scr::runtime;using namespace scr::test::enhanced;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,"Invalid Enhanced input was accepted");}
void rankings(){
    EnhancedPersonalBests pbs(catalog);auto initial=best(120499);check(pbs.offer(initial),"First Enhanced PB was rejected");const auto held=pbs.best(initial.key);
    auto tied=best(120001,false);check(!pbs.offer(tied)&&pbs.best(initial.key)==held,"Invisible microsecond improvement replaced a PB/ghost");
    auto faster=best(119499,false);check(pbs.offer(faster)&&!pbs.best(initial.key)->recording,"Time-only improvement lost its PB");
    auto uncertain=best(1000);uncertain.time.confidence=scr::timing::Reason::missingPrevious;check(!pbs.offer(uncertain),"Uncertain PB ranked");
    check(formatEnhancedTime(time(59999500))=="1:00.000","Millisecond carry formatting differs");
    EnhancedHallOfFame hall(catalog);auto a=attempt(1,120499);check(hall.offer(a)==EnhancedAdmission::Accepted,"First Hall result rejected");
    auto b=attempt(2,120001);hall.offer(b);check(hall.rank({},0)==1&&hall.rank({},1)==1&&hall.entries({})[0].attempt.id==a.id,"Hall tie order or rank uses hidden microseconds");
    check(hall.offer(a)==EnhancedAdmission::Duplicate,"Retry duplicated an attempt");a.time=time(120498);rejected([&]{hall.offer(a);});
    for(unsigned n=3;n<=20;++n)hall.offer(attempt(n,120000));check(hall.offer(attempt(21,119999))==EnhancedAdmission::OutsideTable,"Cutoff tie displaced an existing record");
    check(hall.offer(attempt(22,119499))==EnhancedAdmission::Accepted,"Displayed millisecond improvement was rejected");
    auto invalid=attempt(23);invalid.time.hasEstimate=false;check(hall.offer(invalid)==EnhancedAdmission::Ineligible,"Unavailable estimate ranked");
    auto value=best();value.catalog.timingMethod=2;rejected([&]{pbs.offer(value);});
    auto snapshot=hall.snapshot();snapshot.boards[0][0].insertionOrder=0;rejected([&]{EnhancedHallOfFame other(snapshot);});
    auto inconsistent=time();inconsistent.penaltyMicroseconds=1;rejected([&]{validateEnhancedTime(inconsistent);});
    inconsistent=time();inconsistent.recoveries=std::numeric_limits<std::uint64_t>::max();rejected([&]{validateEnhancedTime(inconsistent);});
}
void codecs(){
    for(bool recording:{false,true})for(unsigned track:{0u,5u}){
        auto pb=best(360000,recording,track);auto bytes=encodeEnhancedBest(pb);const auto restored=decodeEnhancedBest(bytes,pb.key,catalog);
        check(sameEnhancedTime(restored.time,pb.time)&&bool(restored.recording)==recording,"PB round trip changed metadata");
        auto wrong=pb.key;wrong.superLeague=true;rejected([&]{decodeEnhancedBest(bytes,wrong,catalog);});
        auto corrupt=bytes;corrupt.back()^=1;rejected([&]{decodeEnhancedBest(corrupt);});
        auto future=bytes;future[4]=2;rejected([&]{decodeEnhancedBest(future);});
        auto a=attempt(1,360000,{track,HallLeague::Super,HallRecordKind::Lap});auto g=ghost(a);g.recording=recording?scr::test::enhanced::recording(360000,track,1):g.recording;
        if(recording)a.time=g.recording.time;
        const auto encoded=encodeEnhancedHallGhost(g);const auto decoded=decodeEnhancedHallGhost(encoded);requireEnhancedGhostBinding(decoded,a,catalog);
        check(decoded.recording.recoveryOffsets.size()==(recording?1u:0u),"Recovery event timeline was lost");
        auto wrongCatalog=catalog;++wrongCatalog.rules;rejected([&]{requireEnhancedGhostBinding(decoded,a,wrongCatalog);});
    }
    EnhancedHallOfFame hall(catalog);for(unsigned n=1;n<=20;++n)hall.offer(attempt(n,120000+n));
    const auto encoded=encodeEnhancedHall(hall);const auto restored=decodeEnhancedHall(encoded,catalog);check(restored.entries({}).size()==20,"Hall codec lost rows");
    rejected([&]{decodeEnhancedHall(encodeHallTable(HallOfFame(HallCatalog{catalog.trackContent,1})),catalog);});
    rejected([&]{decodeHallTable(encoded,{catalog.trackContent,1});});
    auto g=recording(360000,0,1);g.recoveryOffsets[0]=180000;rejected([&]{validateEnhancedRecording(g,0);});
    g=recording();g.finish.recoveries=1;rejected([&]{validateEnhancedRecording(g,0);});
}
void bounds(){
    EnhancedHallOfFame full(catalog);for(unsigned board=0;board<hallBoardCount;++board)for(unsigned n=0;n<hallEntriesPerBoard;++n){auto a=attempt(board*20+n+1,120000+n,hallBoardKey(board));a.name="ABCDEFGHIJKL";
        if(a.board.kind==HallRecordKind::Lap){a.ghost.emplace();a.ghost->fill(1);}full.offer(a);}
    const auto encoded=encodeEnhancedHall(full);check(encoded.size()==enhancedHallTableMaximumBytes,"Hall maximum encoded size differs from derived bound");
    check(decodeEnhancedHall(encoded,catalog).snapshot().nextInsertionOrder==641,"Maximum Hall round trip failed");
    auto b=best(120000,false,5);auto g=recording((maxGhostLapSamples-2)*scr::timing::tickUs,5,maxGhostLapSamples);
    g.bridges.resize(maxGhostLapBridgeShapes,g.bridges[0]);for(auto& bridge:g.bridges){bridge.cachedSections.resize(510);for(unsigned k=0;k<510;k+=3)bridge.cachedSections[k]=0x33;}
    b.time=g.time;b.recording=std::make_shared<const EnhancedRecording>(g);const auto bytes=encodeEnhancedBest(b);
    check(bytes.size()==enhancedBestMaximumBytes,"PB maximum encoded size differs from derived bound");
    check(decodeEnhancedBest(bytes).recording->samples.size()==maxGhostLapSamples,"Maximum PB round trip failed");
    auto a=attempt(123);a.board.track=5;a.time=g.time;auto ghost=scr::test::enhanced::ghost(a);ghost.recording=std::move(g);
    check(encodeEnhancedHallGhost(ghost).size()==enhancedHallGhostMaximumBytes,"Ghost maximum encoded size differs from derived bound");
}
}
int main()try{rankings();codecs();bounds();std::cout<<"Enhanced models/codecs: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

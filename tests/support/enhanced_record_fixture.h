#pragma once
#include "persistence/enhanced_record_codec.h"

namespace scr::test::enhanced {
using namespace scr::runtime;
inline constexpr EnhancedCatalog catalog{0x123456789abcdef0ULL,1,1};
inline HallAttemptId id(unsigned n){HallAttemptId value{};for(unsigned i=0;i<4;++i)value[i]=std::uint8_t(n>>(8*i));return value;}
inline EnhancedTime time(std::int64_t elapsed=120000,std::uint64_t recoveries=0){
    EnhancedTime value;value.elapsedMicroseconds=elapsed;value.recoveries=recoveries;value.penaltyMicroseconds=std::int64_t(recoveries)*recoveryPenaltyMicroseconds;
    value.scoredMicroseconds=elapsed+value.penaltyMicroseconds;value.confidence=scr::timing::Reason::reliable;value.hasEstimate=true;return value;
}
inline EnhancedRecording recording(std::int64_t elapsed=120000,unsigned track=0,std::uint64_t recoveries=0){
    EnhancedRecording g;g.time=time(elapsed,recoveries);g.start.crossingMicroseconds=60000;g.start.observedMicroseconds=120000;
    g.start.player=g.finish.player=11;g.start.confidence=g.finish.confidence=scr::timing::Reason::reliable;
    g.finish.crossingMicroseconds=g.start.crossingMicroseconds+elapsed;g.finish.observedMicroseconds=((g.finish.crossingMicroseconds+119999)/120000)*120000;g.finish.recoveries=recoveries;
    if(track==5){GhostBridgeShape b;b.cachedSections={0x33,900,901};g.bridges.push_back(b);}
    for(std::int64_t at=-60000;;at+=120000){GhostLapSample sample;sample.microseconds=at;sample.pose.worldPosition={100,200,std::uint32_t(at+60000)};
        sample.segment=0;sample.cut=recoveries&&at==60000;sample.bridge=track==5?0:noGhostBridge;g.samples.push_back(sample);if(at>=elapsed)break;}
    g.recoveryOffsets.assign(std::size_t(recoveries),60000);return g;
}
inline EnhancedHallAttempt attempt(unsigned n,std::int64_t elapsed=120000,HallBoardKey board={}){return {id(n),board,"Craig",time(elapsed),{}};}
inline EnhancedHallGhost ghost(const EnhancedHallAttempt& a){return {catalog,a.id,a.board,recording(a.time.elapsedMicroseconds,a.board.track,a.time.recoveries)};}
inline EnhancedBest best(std::int64_t elapsed=120000,bool withGhost=true,unsigned track=0){
    EnhancedBest b;b.catalog=catalog;b.key={std::string(32,'1'),track,false};b.time=time(elapsed);
    if(withGhost)b.recording=std::make_shared<const EnhancedRecording>(recording(elapsed,track));return b;
}
} // namespace scr::test::enhanced

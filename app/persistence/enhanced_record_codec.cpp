#include "persistence/enhanced_record_codec.h"
#include <algorithm>
#include <limits>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t tableMagic=0x4c414845,bestMagic=0x42504553,ghostMagic=0x47484553;
void check(bool value){if(!value)throw std::runtime_error("Invalid Enhanced record format");}
std::int64_t signedTime(std::uint64_t value){constexpr auto high=std::uint64_t(1)<<63;return value<high?std::int64_t(value):std::numeric_limits<std::int64_t>::min()+std::int64_t(value-high);}
void seal(Writer& w,std::size_t maximum){w.u64(hash(w.data.data(),w.data.size()));check(w.data.size()<=maximum);}
void header(Reader& r,std::uint32_t magic,std::size_t maximum){
    check(r.data.size()>=24&&r.data.size()<=maximum);check(r.u32()==magic);
    if(r.u32()!=enhancedRecordFormatVersion)throw UnsupportedEnhancedRecords("Unsupported Enhanced record format");
    Reader tail{r.data,r.data.size()-8};check(tail.u64()==hash(r.data.data(),r.data.size()-8));
}
void end(Reader& r){check(r.position==r.data.size()-8);(void)r.u64();r.end();}
void catalog(Writer& w,EnhancedCatalog c){validateEnhancedCatalog(c);w.u32(c.rules);w.u32(c.timingMethod);w.u64(c.trackContent);}
EnhancedCatalog catalog(Reader& r){EnhancedCatalog c;c.rules=r.u32();c.timingMethod=r.u32();c.trackContent=r.u64();validateEnhancedCatalog(c);return c;}
void expected(EnhancedCatalog actual,EnhancedCatalog want){if(!sameEnhancedCatalog(actual,want))throw UnsupportedEnhancedRecords("Enhanced record belongs to different content, rules or timing");}
void time(Writer& w,const EnhancedTime& t){validateEnhancedTime(t);w.u32(t.methodVersion);w.u64(std::uint64_t(t.elapsedMicroseconds));w.u64(std::uint64_t(t.penaltyMicroseconds));w.u64(std::uint64_t(t.scoredMicroseconds));w.u64(t.recoveries);w.u32(unsigned(t.confidence));w.u32(t.hasEstimate);}
EnhancedTime time(Reader& r){EnhancedTime t;t.methodVersion=r.u32();t.elapsedMicroseconds=signedTime(r.u64());t.penaltyMicroseconds=signedTime(r.u64());t.scoredMicroseconds=signedTime(r.u64());t.recoveries=r.u64();
    const auto confidence=r.u32(),estimate=r.u32();check(confidence==unsigned(timing::Reason::reliable)&&estimate==1);t.confidence=timing::Reason::reliable;t.hasEstimate=true;validateEnhancedTime(t);return t;}
void boundary(Writer& w,const LapTimingBoundary& b){w.u64(std::uint64_t(b.observedMicroseconds));w.u64(std::uint64_t(b.crossingMicroseconds));w.u64(b.recoveries);w.u64(b.recoveryEpoch);w.u32(b.player);w.u32(unsigned(b.confidence));}
LapTimingBoundary boundary(Reader& r){LapTimingBoundary b;b.observedMicroseconds=signedTime(r.u64());b.crossingMicroseconds=signedTime(r.u64());b.recoveries=r.u64();b.recoveryEpoch=r.u64();b.player=r.u32();const auto confidence=r.u32();check(confidence==unsigned(timing::Reason::reliable));b.confidence=timing::Reason::reliable;return b;}
void recording(Writer& w,const EnhancedRecording& g){
    time(w,g.time);boundary(w,g.start);boundary(w,g.finish);w.u32(unsigned(g.samples.size()));w.u32(unsigned(g.bridges.size()));w.u32(unsigned(g.recoveryOffsets.size()));
    for(const auto& s:g.samples){w.u64(std::uint64_t(s.microseconds));for(auto value:s.pose.worldPosition)w.u32(value);
        w.u32((unsigned(s.pose.pitch)<<16)|s.pose.yaw);w.u32((unsigned(s.pose.roll)<<16)|s.pose.averageWheelVelocity);
        w.u32((unsigned(s.bridge)<<16)|(unsigned(s.region)<<8)|s.segment);w.u32(s.cut);}
    for(const auto& b:g.bridges){w.bytes(b.nearGeometry.data(),b.nearGeometry.size());w.u32(unsigned(b.cachedSections.size()));for(auto value:b.cachedSections)w.u32(value);}
    for(auto offset:g.recoveryOffsets)w.u64(std::uint64_t(offset));
}
EnhancedRecording recording(Reader& r){
    EnhancedRecording g;g.time=time(r);g.start=boundary(r);g.finish=boundary(r);const auto samples=r.u32(),shapes=r.u32(),events=r.u32();
    check(samples>=2&&samples<=maxGhostLapSamples&&shapes<=maxGhostLapBridgeShapes&&events<=maxGhostLapSamples);r.require(std::size_t(samples)*36);g.samples.reserve(samples);
    for(unsigned n=0;n<samples;++n){GhostLapSample s;s.microseconds=signedTime(r.u64());for(auto& value:s.pose.worldPosition)value=r.u32();
        const auto a=r.u32(),b=r.u32(),c=r.u32(),cut=r.u32();check(cut<=1&&((c>>8)&255)<=3);
        s.pose.pitch=std::uint16_t(a>>16);s.pose.yaw=std::uint16_t(a);s.pose.roll=std::uint16_t(b>>16);s.pose.averageWheelVelocity=std::uint16_t(b);
        s.bridge=std::uint16_t(c>>16);s.region=GhostBridgeRegion((c>>8)&255);s.segment=std::uint8_t(c);s.cut=cut!=0;g.samples.push_back(s);}
    g.bridges.reserve(shapes);for(unsigned n=0;n<shapes;++n){GhostBridgeShape b;r.bytes(b.nearGeometry.data(),b.nearGeometry.size());const auto count=r.u32();check(count&&count<=510&&count%3==0);r.require(std::size_t(count)*4);
        b.cachedSections.reserve(count);for(unsigned k=0;k<count;++k){const auto value=r.u32();check(value<=65535);b.cachedSections.push_back(std::uint16_t(value));}g.bridges.push_back(std::move(b));}
    r.require(std::size_t(events)*8);g.recoveryOffsets.reserve(events);for(unsigned n=0;n<events;++n)g.recoveryOffsets.push_back(signedTime(r.u64()));return g;
}
}
state::checkpoint::Bytes encodeEnhancedHall(const EnhancedHallOfFame& hall){
    const auto& state=hall.snapshot();Writer w;w.u32(tableMagic);w.u32(enhancedRecordFormatVersion);catalog(w,state.catalog);w.u64(state.nextInsertionOrder);
    for(const auto& board:state.boards){w.u32(unsigned(board.size()));for(const auto& e:board){w.bytes(e.attempt.id.data(),e.attempt.id.size());w.text(e.attempt.name);time(w,e.attempt.time);w.u64(e.insertionOrder);w.u32(e.attempt.ghost?1:0);if(e.attempt.ghost)w.bytes(e.attempt.ghost->data(),e.attempt.ghost->size());}}
    seal(w,enhancedHallTableMaximumBytes);return std::move(w.data);
}
EnhancedHallOfFame decodeEnhancedHall(const state::checkpoint::Bytes& bytes,EnhancedCatalog want){
    Reader r{bytes};header(r,tableMagic,enhancedHallTableMaximumBytes);EnhancedHallSnapshot state;state.catalog=catalog(r);expected(state.catalog,want);state.nextInsertionOrder=r.u64();
    for(unsigned n=0;n<hallBoardCount;++n){const auto count=r.u32();check(count<=hallEntriesPerBoard);for(unsigned k=0;k<count;++k){EnhancedHallEntry e;e.attempt.board=hallBoardKey(n);r.bytes(e.attempt.id.data(),e.attempt.id.size());e.attempt.name=r.text(12);e.attempt.time=time(r);e.insertionOrder=r.u64();const auto ghost=r.u32();check(ghost<=1);if(ghost){e.attempt.ghost.emplace();r.bytes(e.attempt.ghost->data(),e.attempt.ghost->size());}state.boards[n].push_back(std::move(e));}}
    end(r);return EnhancedHallOfFame(std::move(state));
}
state::checkpoint::Bytes encodeEnhancedBest(const EnhancedBest& best){
    validateEnhancedBest(best);Writer w;w.u32(bestMagic);w.u32(enhancedRecordFormatVersion);catalog(w,best.catalog);w.text(best.key.career);w.u32(best.key.track);w.u32(best.key.superLeague);time(w,best.time);w.u32(bool(best.recording));if(best.recording)recording(w,*best.recording);seal(w,enhancedBestMaximumBytes);return std::move(w.data);
}
EnhancedBest decodeEnhancedBest(const state::checkpoint::Bytes& bytes){
    Reader r{bytes};header(r,bestMagic,enhancedBestMaximumBytes);EnhancedBest best;best.catalog=catalog(r);best.key.career=r.text(32);best.key.track=r.u32();const auto league=r.u32();check(league<=1);best.key.superLeague=league!=0;best.time=time(r);const auto present=r.u32();check(present<=1);if(present)best.recording=std::make_shared<const EnhancedRecording>(recording(r));end(r);validateEnhancedBest(best);return best;
}
EnhancedBest decodeEnhancedBest(const state::checkpoint::Bytes& bytes,const EnhancedBestKey& key,EnhancedCatalog want){
    auto best=decodeEnhancedBest(bytes);expected(best.catalog,want);if(best.key<key||key<best.key)throw UnsupportedEnhancedRecords("Enhanced PB belongs to a different career, track or league");return best;
}
state::checkpoint::Bytes encodeEnhancedHallGhost(const EnhancedHallGhost& ghost){
    validateEnhancedCatalog(ghost.catalog);check(ghost.board.kind==HallRecordKind::Lap);(void)hallBoardIndex(ghost.board);check(std::any_of(ghost.attempt.begin(),ghost.attempt.end(),[](auto b){return b!=0;}));validateEnhancedRecording(ghost.recording,ghost.board.track);
    Writer w;w.u32(ghostMagic);w.u32(enhancedRecordFormatVersion);catalog(w,ghost.catalog);w.bytes(ghost.attempt.data(),ghost.attempt.size());w.u32(hallBoardIndex(ghost.board));recording(w,ghost.recording);seal(w,enhancedHallGhostMaximumBytes);return std::move(w.data);
}
EnhancedHallGhost decodeEnhancedHallGhost(const state::checkpoint::Bytes& bytes){
    Reader r{bytes};header(r,ghostMagic,enhancedHallGhostMaximumBytes);EnhancedHallGhost ghost;ghost.catalog=catalog(r);r.bytes(ghost.attempt.data(),ghost.attempt.size());ghost.board=hallBoardKey(r.u32());ghost.recording=recording(r);end(r);
    check(ghost.board.kind==HallRecordKind::Lap&&std::any_of(ghost.attempt.begin(),ghost.attempt.end(),[](auto b){return b!=0;}));validateEnhancedRecording(ghost.recording,ghost.board.track);return ghost;
}
void requireEnhancedGhostBinding(const EnhancedHallGhost& ghost,const EnhancedHallAttempt& attempt,EnhancedCatalog want){
    expected(ghost.catalog,want);check(ghost.attempt==attempt.id&&ghost.board.kind==HallRecordKind::Lap&&hallBoardIndex(ghost.board)==hallBoardIndex(attempt.board)&&sameEnhancedTime(ghost.recording.time,attempt.time));
}
} // namespace scr::runtime

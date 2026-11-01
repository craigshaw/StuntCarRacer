#include "persistence/hall_ghost_codec.h"
#include <algorithm>
#include <limits>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x54534848;
constexpr std::int64_t tick=120000;
void check(bool value){if(!value)throw std::runtime_error("Invalid Hall ghost");}
std::int64_t signedTime(std::uint64_t v){
    constexpr auto high=std::uint64_t(1)<<63;
    return v<high?std::int64_t(v):std::numeric_limits<std::int64_t>::min()+std::int64_t(v-high);
}
void boundary(Writer& w,const GhostLapBoundary& b){
    w.u64(std::uint64_t(b.observedMicroseconds));w.u64(std::uint64_t(b.alignedMicroseconds));w.u32(b.progress);
    w.u32(b.correctionHundredths);w.u32(unsigned(b.interpolated)|(unsigned(b.recovery)<<1));
}
GhostLapBoundary boundary(Reader& r){
    GhostLapBoundary b;b.observedMicroseconds=signedTime(r.u64());b.alignedMicroseconds=signedTime(r.u64());
    const auto p=r.u32(),c=r.u32(),f=r.u32();check(p<=65535&&c<=12&&f<=3);
    b.progress=std::uint16_t(p);b.correctionHundredths=std::uint8_t(c);b.interpolated=f&1;b.recovery=f&2;return b;
}
void validate(const HallGhost& g){
    check(g.catalog.trackContent&&g.catalog.rules&&g.board.kind==HallRecordKind::Lap);
    (void)hallBoardIndex(g.board);(void)hallTimeHundredths(g.time,HallRecordKind::Lap);
    check(std::any_of(g.attempt.begin(),g.attempt.end(),[](auto b){return b!=0;}));
    check(g.durationMicroseconds>0&&g.durationMicroseconds<=std::int64_t(maxGhostLapSamples)*tick);
    for(const auto* b:{&g.start,&g.finish})check(b->alignedMicroseconds>=0&&b->observedMicroseconds>=b->alignedMicroseconds&&
        b->observedMicroseconds-b->alignedMicroseconds<=tick&&(!b->recovery||!b->interpolated));
    const auto correction=[](const auto& b){return b.progress<256?((unsigned(b.progress)^255)*13)>>8:0;};
    check((g.start.correctionHundredths==0||g.start.correctionHundredths==correction(g.start))&&
        g.finish.correctionHundredths==correction(g.finish)&&g.finish.alignedMicroseconds>=g.start.alignedMicroseconds&&
        g.finish.alignedMicroseconds-g.start.alignedMicroseconds==g.durationMicroseconds);
    check(g.samples.size()>=2&&g.samples.size()<=maxGhostLapSamples&&g.bridges.size()<=maxGhostLapBridgeShapes);
    check(g.samples.front().microseconds>=-tick&&g.samples.front().microseconds<=0&&
        g.samples.back().microseconds>=g.durationMicroseconds&&g.samples.back().microseconds<=g.durationMicroseconds+tick);
    auto previous=g.samples.front().microseconds;
    for(const auto& s:g.samples){
        check(s.microseconds>=previous&&s.microseconds<=g.durationMicroseconds+tick&&s.microseconds-previous<=tick&&s.segment<100&&unsigned(s.region)<=3);
        check(s.bridge==noGhostBridge||s.bridge<g.bridges.size());
        check(g.board.track==5?s.bridge!=noGhostBridge:s.bridge==noGhostBridge&&s.region==GhostBridgeRegion::outside);previous=s.microseconds;
    }
    check(g.board.track==5?!g.bridges.empty():g.bridges.empty());
    for(const auto& b:g.bridges){check(!b.cachedSections.empty()&&b.cachedSections.size()<=510&&b.cachedSections.size()%3==0);
        for(unsigned n=0;n<b.cachedSections.size();n+=3)check((b.cachedSections[n]&255)>=0x33&&(b.cachedSections[n]&255)<0x38);}
}
}
state::checkpoint::Bytes encodeHallGhost(const HallGhost& g){
    validate(g);Writer w;w.u32(magic);w.u32(hallGhostFormatVersion);w.u32(g.catalog.rules);w.u64(g.catalog.trackContent);
    w.bytes(g.attempt.data(),g.attempt.size());w.u32(hallBoardIndex(g.board));
    w.u32((unsigned(g.time.minutes)<<16)|(unsigned(g.time.seconds)<<8)|g.time.subseconds);
    w.u64(std::uint64_t(g.durationMicroseconds));boundary(w,g.start);boundary(w,g.finish);
    w.u32(unsigned(g.samples.size()));w.u32(unsigned(g.bridges.size()));
    for(const auto& s:g.samples){w.u64(std::uint64_t(s.microseconds));for(auto v:s.pose.worldPosition)w.u32(v);
        w.u32((unsigned(s.pose.pitch)<<16)|s.pose.yaw);w.u32((unsigned(s.pose.roll)<<16)|s.pose.averageWheelVelocity);
        w.u32((unsigned(s.bridge)<<16)|(unsigned(s.region)<<8)|s.segment);w.u32(s.cut);}
    for(const auto& b:g.bridges){w.bytes(b.nearGeometry.data(),b.nearGeometry.size());w.u32(unsigned(b.cachedSections.size()));for(auto v:b.cachedSections)w.u32(v);}
    w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
HallGhost decodeHallGhost(const state::checkpoint::Bytes& bytes){
    check(bytes.size()<=hallGhostMaximumBytes&&bytes.size()>=124);Reader r{bytes};check(r.u32()==magic);
    if(r.u32()!=hallGhostFormatVersion)throw UnsupportedHallGhost("Unsupported Hall ghost format");
    HallGhost g;g.catalog.rules=r.u32();g.catalog.trackContent=r.u64();r.bytes(g.attempt.data(),g.attempt.size());g.board=hallBoardKey(r.u32());
    const auto time=r.u32();check(time<=0xffffff);g.time={std::uint8_t(time>>16),std::uint8_t(time>>8),std::uint8_t(time)};
    g.durationMicroseconds=signedTime(r.u64());g.start=boundary(r);g.finish=boundary(r);
    const auto count=r.u32(),shapes=r.u32();check(count>=2&&count<=maxGhostLapSamples&&shapes<=maxGhostLapBridgeShapes);
    r.require(std::size_t(count)*36);g.samples.reserve(count);
    for(unsigned n=0;n<count;++n){GhostLapSample s;s.microseconds=signedTime(r.u64());for(auto& v:s.pose.worldPosition)v=r.u32();
        const auto a=r.u32(),b=r.u32(),c=r.u32(),cut=r.u32();check(cut<=1&&((c>>8)&255)<=3);
        s.pose.pitch=std::uint16_t(a>>16);s.pose.yaw=std::uint16_t(a);s.pose.roll=std::uint16_t(b>>16);s.pose.averageWheelVelocity=std::uint16_t(b);
        s.bridge=std::uint16_t(c>>16);s.region=GhostBridgeRegion((c>>8)&255);s.segment=std::uint8_t(c);s.cut=cut!=0;g.samples.push_back(s);}
    g.bridges.reserve(shapes);
    for(unsigned n=0;n<shapes;++n){GhostBridgeShape b;r.bytes(b.nearGeometry.data(),b.nearGeometry.size());const auto size=r.u32();check(size&&size<=510&&size%3==0);
        r.require(std::size_t(size)*4);b.cachedSections.reserve(size);for(unsigned i=0;i<size;++i){const auto v=r.u32();check(v<=65535);b.cachedSections.push_back(std::uint16_t(v));}g.bridges.push_back(std::move(b));}
    const auto end=r.position;check(r.u64()==hash(bytes.data(),end));r.end();validate(g);return g;
}
void requireHallGhostBinding(const HallGhost& g,const HallAttempt& a,HallCatalog c){
    if(g.catalog.trackContent!=c.trackContent||g.catalog.rules!=c.rules)throw UnsupportedHallGhost("Hall ghost uses different content or rules");
    check(g.attempt==a.id&&hallBoardIndex(g.board)==hallBoardIndex(a.board)&&g.time.minutes==a.time.minutes&&g.time.seconds==a.time.seconds&&g.time.subseconds==a.time.subseconds);
}
} // namespace scr::runtime

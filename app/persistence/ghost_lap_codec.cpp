#include "persistence/ghost_lap_codec.h"
#include <algorithm>
#include <limits>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x54534847;
constexpr std::int64_t tick=120000;
void check(bool value){if(!value)throw std::runtime_error("Invalid ghost recording");}
std::int64_t signedTime(std::uint64_t value){
    constexpr auto high=std::uint64_t(1)<<63;
    return value<high?std::int64_t(value):std::numeric_limits<std::int64_t>::min()+std::int64_t(value-high);
}
void boundary(Writer& w,const GhostLapBoundary& b){
    w.u64(std::uint64_t(b.observedMicroseconds));w.u64(std::uint64_t(b.alignedMicroseconds));w.u32(b.progress);
    w.u32(b.correctionHundredths);w.u32(unsigned(b.interpolated)|(unsigned(b.recovery)<<1));
}
GhostLapBoundary boundary(Reader& r){
    GhostLapBoundary b;b.observedMicroseconds=signedTime(r.u64());b.alignedMicroseconds=signedTime(r.u64());
    const auto progress=r.u32(),correction=r.u32(),flags=r.u32();check(progress<=0xffff&&correction<=12&&flags<=3);
    b.progress=std::uint16_t(progress);b.correctionHundredths=std::uint8_t(correction);b.interpolated=flags&1;b.recovery=flags&2;return b;
}
void validate(const GhostLap& lap){
    const auto& key=lap.context.key;
    check(key.career.size()==32&&std::all_of(key.career.begin(),key.career.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}));
    check(key.track<8&&lap.context.trackContentIdentity&&unsigned(lap.context.source)<=1&&lap.controlPresets>=1&&lap.controlPresets<=3);
    check(lap.hundredths==ghostLapHundredths(lap.completedTime)&&lap.durationMicroseconds>0&&lap.durationMicroseconds<=maxGhostLapSamples*tick);
    for(const auto* b:{&lap.start,&lap.finish})check(b->alignedMicroseconds>=0&&b->observedMicroseconds>=b->alignedMicroseconds&&
        b->observedMicroseconds-b->alignedMicroseconds<=tick&&(!b->recovery||!b->interpolated));
    const auto startCorrection=lap.start.progress<256?((unsigned(lap.start.progress)^255)*13)>>8:0;
    check((lap.start.correctionHundredths==0||lap.start.correctionHundredths==startCorrection)&&lap.finish.alignedMicroseconds>=lap.start.alignedMicroseconds&&
        lap.finish.alignedMicroseconds-lap.start.alignedMicroseconds==lap.durationMicroseconds);
    const auto correction=lap.finish.progress<256?((unsigned(lap.finish.progress)^255)*13)>>8:0;
    check(lap.finish.correctionHundredths==correction);
    check(lap.samples.size()>=2&&lap.samples.size()<=maxGhostLapSamples&&lap.bridges.size()<=maxGhostLapBridgeShapes);
    check(lap.samples.front().microseconds>=-tick&&lap.samples.front().microseconds<=0&&
        lap.samples.back().microseconds>=lap.durationMicroseconds&&lap.samples.back().microseconds<=lap.durationMicroseconds+tick);
    std::int64_t previous=lap.samples.front().microseconds;
    for(const auto& s:lap.samples){
        check(s.microseconds>=previous&&s.microseconds<=lap.durationMicroseconds+tick&&s.microseconds-previous<=tick&&s.segment<100&&unsigned(s.region)<=3);
        check(s.bridge==noGhostBridge||s.bridge<lap.bridges.size());
        check(key.track==5?s.bridge!=noGhostBridge:s.bridge==noGhostBridge&&s.region==GhostBridgeRegion::outside);previous=s.microseconds;
    }
    check(key.track==5?!lap.bridges.empty():lap.bridges.empty());
    for(const auto& bridge:lap.bridges){
        check(!bridge.cachedSections.empty()&&bridge.cachedSections.size()<=512&&bridge.cachedSections.size()%3==0);
        for(unsigned n=0;n<bridge.cachedSections.size();n+=3)check((bridge.cachedSections[n]&255)>=0x33&&(bridge.cachedSections[n]&255)<0x38);
    }
}
}
state::checkpoint::Bytes encodeGhostLap(const GhostLap& lap){
    validate(lap);Writer w;w.u32(magic);w.u32(ghostLapFormatVersion);w.u32(ghostLapRulesVersion);w.u64(lap.context.trackContentIdentity);
    w.text(lap.context.key.career);w.u32(lap.context.key.track);w.u32(lap.context.key.superLeague);w.u32(unsigned(lap.context.source));w.u32(lap.controlPresets);
    w.u32((unsigned(lap.completedTime.minutes)<<16)|(unsigned(lap.completedTime.seconds)<<8)|lap.completedTime.subseconds);
    w.u64(std::uint64_t(lap.durationMicroseconds));boundary(w,lap.start);boundary(w,lap.finish);
    w.u32(unsigned(lap.samples.size()));w.u32(unsigned(lap.bridges.size()));
    for(const auto& s:lap.samples){
        w.u64(std::uint64_t(s.microseconds));for(auto value:s.pose.worldPosition)w.u32(value);
        w.u32((unsigned(s.pose.pitch)<<16)|s.pose.yaw);w.u32((unsigned(s.pose.roll)<<16)|s.pose.averageWheelVelocity);
        w.u32((unsigned(s.bridge)<<16)|(unsigned(s.region)<<8)|s.segment);w.u32(s.cut);
    }
    for(const auto& b:lap.bridges){w.bytes(b.nearGeometry.data(),b.nearGeometry.size());w.u32(unsigned(b.cachedSections.size()));for(auto value:b.cachedSections)w.u32(value);}
    return std::move(w.data);
}
GhostLap decodeGhostLap(const state::checkpoint::Bytes& data){
    check(data.size()<=4*1024*1024);Reader r{data};check(r.u32()==magic);
    if(r.u32()!=ghostLapFormatVersion||r.u32()!=ghostLapRulesVersion)throw UnsupportedGhostLap("Unsupported ghost format or race rules");
    GhostLap lap;lap.context.trackContentIdentity=r.u64();lap.context.key.career=r.text(32);lap.context.key.track=r.u32();
    const auto league=r.u32(),source=r.u32();check(league<=1&&source<=1);lap.context.key.superLeague=league!=0;lap.context.source=GhostLapSource(source);
    lap.controlPresets=r.u32();const auto time=r.u32();check(time<=0xffffff);
    lap.completedTime={std::uint8_t(time>>16),std::uint8_t(time>>8),std::uint8_t(time)};lap.hundredths=ghostLapHundredths(lap.completedTime);
    lap.durationMicroseconds=signedTime(r.u64());lap.start=boundary(r);lap.finish=boundary(r);
    const auto samples=r.u32(),bridges=r.u32();check(samples>=2&&samples<=maxGhostLapSamples&&bridges<=maxGhostLapBridgeShapes);
    r.require(std::size_t(samples)*36);lap.samples.reserve(samples);
    for(unsigned n=0;n<samples;++n){
        GhostLapSample s;s.microseconds=signedTime(r.u64());for(auto& value:s.pose.worldPosition)value=r.u32();
        const auto a=r.u32(),b=r.u32(),c=r.u32(),cut=r.u32();check(cut<=1&&((c>>8)&255)<=3);
        s.pose.pitch=std::uint16_t(a>>16);s.pose.yaw=std::uint16_t(a);s.pose.roll=std::uint16_t(b>>16);s.pose.averageWheelVelocity=std::uint16_t(b);
        s.bridge=std::uint16_t(c>>16);s.region=GhostBridgeRegion((c>>8)&255);s.segment=std::uint8_t(c);s.cut=cut!=0;lap.samples.push_back(s);
    }
    lap.bridges.reserve(bridges);
    for(unsigned n=0;n<bridges;++n){
        GhostBridgeShape shape;r.bytes(shape.nearGeometry.data(),shape.nearGeometry.size());const auto count=r.u32();check(count&&count<=512&&count%3==0);r.require(std::size_t(count)*4);
        shape.cachedSections.reserve(count);for(unsigned i=0;i<count;++i){const auto value=r.u32();check(value<=0xffff);shape.cachedSections.push_back(std::uint16_t(value));}lap.bridges.push_back(std::move(shape));
    }
    r.end();validate(lap);return lap;
}
GhostLap decodeGhostLap(const state::checkpoint::Bytes& data,const GhostLapKey& key,std::uint64_t content){
    auto lap=decodeGhostLap(data);
    if(lap.context.key<key||key<lap.context.key||lap.context.trackContentIdentity!=content)throw UnsupportedGhostLap("Ghost belongs to different career, track or track data");
    return lap;
}
} // namespace scr::runtime

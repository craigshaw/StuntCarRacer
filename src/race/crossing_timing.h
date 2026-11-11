#pragma once
// Pure crossing measurement on the native fixed-point timeline; no state writes.
#include "track/track_progress.h"
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace scr::timing {
constexpr std::int64_t tickUs=120000,cellSize=0x800000,worldSize=16*cellSize;
struct Gate {
    unsigned axis{},transverse{},lap{},previous{},count{};
    int direction{};
    std::int64_t coordinate{},transverseMinimum{};
    bool valid{};
};
inline Gate gate(const TrackProgressData& data,unsigned lap)try{
    Gate g;
    if(data.track.segmentCount<2||data.track.segmentCount>100||lap>=data.track.segmentCount)return g;
    g.count=data.track.segmentCount;g.lap=lap;g.previous=(lap+g.count-1)%g.count;
    const auto a=loadTrackSegmentConfiguration(data.track,std::uint8_t(lap));
    const auto b=loadTrackSegmentConfiguration(data.track,std::uint8_t(g.previous));
    // The supplied tracks all have this configuration at both sides of the line.
    if(a.geometryType||b.geometryType||a.steeringFlags||b.steeringFlags||a.reverseDirection||b.reverseDirection||
       a.mode!=8||b.mode!=8||a.slopeFlags!=b.slopeFlags)return g;
    const unsigned heading=a.slopeFlags>>6;
    g.axis=(heading&1)?0:2;g.transverse=2-g.axis;g.direction=heading<2?1:-1;
    const auto cell=data.segmentCoordinates[lap],before=data.segmentCoordinates[g.previous];
    const int x=cell&15,z=cell>>4,bx=before&15,bz=before>>4;
    if((g.axis==0&&(x-bx!=g.direction||z!=bz))||(g.axis==2&&(z-bz!=g.direction||x!=bx)))return g;
    g.coordinate=(g.axis==0?x:z)*cellSize+(g.direction<0?cellSize:0);
    g.transverseMinimum=(g.transverse==0?x:z)*cellSize;g.valid=true;return g;
}catch(const std::out_of_range&){return {};}catch(const std::invalid_argument&){return {};}
struct Observation {
    std::int64_t us{};
    std::array<std::uint32_t,3> position{};
    unsigned segment{},progress{},restarts{},offTrack{},player{};
    bool found{true},recovery{};
};
enum class Reason { reliable,unaccepted,unsupportedGate,missingPrevious,trackLookup,recovery,interval,segments,position,forwardBracket,gateExtent,player,penaltySequence,arithmetic };
inline const char* name(Reason value){
    switch(value){
    case Reason::reliable:return "reliable";
    case Reason::unaccepted:return "unaccepted";
    case Reason::unsupportedGate:return "unsupported-gate";
    case Reason::missingPrevious:return "missing-previous";
    case Reason::trackLookup:return "track-lookup";
    case Reason::recovery:return "recovery-cut";
    case Reason::interval:return "invalid-interval";
    case Reason::segments:return "nonadjacent-segments";
    case Reason::position:return "invalid-position";
    case Reason::forwardBracket:return "no-forward-bracket";
    case Reason::gateExtent:return "outside-gate-extent";
    case Reason::player:return "player-changed";
    case Reason::penaltySequence:return "unknown-recovery-sequence";
    case Reason::arithmetic:return "time-overflow";
    }return "unknown";
}
inline std::int64_t signedDistance(const Gate& g,const Observation& o){return g.direction*(std::int64_t(o.position[g.axis])-g.coordinate);}
inline std::int64_t divideNearest(std::int64_t numerator,std::int64_t denominator){return (numerator+denominator/2)/denominator;}
struct Estimate {Reason reason{Reason::missingPrevious};std::int64_t offsetUs{};};
inline Estimate world(const Gate& g,const std::optional<Observation>& previous,const Observation& now,bool accepted){
    if(!accepted)return {Reason::unaccepted};
    if(!g.valid||(g.axis!=0&&g.axis!=2)||g.transverse!=2-g.axis||(g.direction!=1&&g.direction!=-1)||
       g.coordinate<0||g.coordinate>worldSize||g.transverseMinimum<0||g.transverseMinimum>worldSize-cellSize||
       g.count<2||g.count>100||g.lap>=g.count||g.previous!=(g.lap+g.count-1)%g.count)return {Reason::unsupportedGate};
    if(!previous)return {Reason::missingPrevious};
    const auto& before=*previous;
    if(!before.found||!now.found||((before.offTrack|now.offTrack)&0x40))return {Reason::trackLookup};
    if(before.recovery||now.recovery||before.restarts!=now.restarts)return {Reason::recovery};
    if(before.us<0||now.us<before.us||now.us-before.us!=tickUs)return {Reason::interval};
    if(before.segment!=g.previous||now.segment!=g.lap)return {Reason::segments};
    for(auto axis:{0u,2u})if(before.position[axis]>=worldSize||now.position[axis]>=worldSize)return {Reason::position};
    const auto d0=signedDistance(g,before),d1=signedDistance(g,now);
    if(d0>0||d1<0||d1<=d0)return {Reason::forwardBracket};
    const auto distance=d1-d0;
    // Compare the exact rational transverse intersection with the finite cell edge.
    const auto transverse=(std::int64_t(before.position[g.transverse])-g.transverseMinimum)*distance+
        (std::int64_t(now.position[g.transverse])-before.position[g.transverse])*(-d0);
    if(transverse<0||transverse>cellSize*distance)return {Reason::gateExtent};
    // Bounds above keep both products below 2^56, well inside signed 64 bits.
    return {Reason::reliable,divideNearest(tickUs*(-d0),distance)};
}
} // namespace scr::timing

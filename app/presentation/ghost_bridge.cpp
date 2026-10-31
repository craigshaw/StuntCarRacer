#include "presentation/ghost_bridge.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::runtime {
GhostBridgeShape captureGhostBridgeShape(const TrackGeometryData& track,const TrackMeshData& mesh,unsigned offset){
    GhostBridgeShape result;
    if(offset>track.geometry.size()||result.nearGeometry.size()>track.geometry.size()-offset)
        throw std::invalid_argument("Ghost bridge geometry outside track data");
    std::copy_n(track.geometry.begin()+offset,result.nearGeometry.size(),result.nearGeometry.begin());
    for(const auto& section:mesh.sections)if(section.segment>=0x33&&section.segment<0x38){
        if(result.cachedSections.size()+3>512)throw std::invalid_argument("Ghost bridge snapshot exceeds section bound");
        result.cachedSections.push_back(std::uint16_t((unsigned(section.flags)<<8)|section.segment));
        result.cachedSections.push_back(section.points[0][2]);result.cachedSections.push_back(section.points[1][2]);
    }
    if(result.cachedSections.empty())throw std::invalid_argument("Ghost bridge snapshot has no moving sections");
    return result;
}
GhostBridgeRegion GhostBridgeRegionTracker::observe(unsigned segment,bool grounded,bool cut){
    if(cut){engaged_=crossed_=false;groundedTicks_=0;}
    if(segment>=0x33&&segment<0x38){engaged_=crossed_=true;groundedTicks_=0;return GhostBridgeRegion::crossing;}
    if(segment>=0x31&&segment<0x33){engaged_=true;groundedTicks_=0;return GhostBridgeRegion::approach;}
    if(engaged_&&crossed_){
        groundedTicks_=grounded?groundedTicks_+1:0;
        if(groundedTicks_<2)return GhostBridgeRegion::landing;
    }
    engaged_=crossed_=false;groundedTicks_=0;return GhostBridgeRegion::outside;
}
double GhostBridgeVisibility::alphaAt(double seconds)const{
    if(!std::isfinite(seconds)||seconds<0)throw std::invalid_argument("Invalid ghost presentation time");
    const auto at=std::upper_bound(transitions_.begin(),transitions_.end(),seconds,
        [](double t,const Transition& key){return t<key.time;});
    if(at==transitions_.begin())return 1;
    const auto& key=*(at-1);const auto fraction=std::clamp((seconds-key.time)/0.24,0.0,1.0);
    return key.from+(key.to-key.from)*fraction;
}
bool GhostBridgeVisibility::conditionsDifferAt(double seconds)const{
    if(!std::isfinite(seconds)||seconds<0)throw std::invalid_argument("Invalid ghost presentation time");
    const auto at=std::upper_bound(transitions_.begin(),transitions_.end(),seconds,
        [](double t,const Transition& key){return t<key.time;});
    return at!=transitions_.begin()&&(at-1)->differs;
}
void GhostBridgeVisibility::observe(double seconds,GhostBridgeRegion region,bool matches,bool cut){
    if(!std::isfinite(seconds)||seconds<0||seconds<lastTime_)throw std::invalid_argument("Ghost observations must be chronological");
    if(cut||region==GhostBridgeRegion::outside)blocked_=false;
    if(region!=GhostBridgeRegion::outside&&!matches)blocked_=true;
    const double target=blocked_?0:1;
    if(transitions_.empty()||transitions_.back().to!=target||cut){
        const auto from=cut?target:alphaAt(seconds);
        transitions_.push_back({seconds,from,target,blocked_});
    }
    lastTime_=seconds;
}
} // namespace scr::runtime

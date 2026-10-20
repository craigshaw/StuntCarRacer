#pragma once
#include "draw_bridge.h"
#include "track_initialization.h"
#include <algorithm>
#include <vector>

namespace scr::test::drawBridge {
using Ram=NativeMemory;
constexpr unsigned entry=0x5a794,end=0x5a996;
inline std::uint16_t readWord(const Ram& r,unsigned a){return std::uint16_t((unsigned(r.at(a))<<8)|r.at(a+1));}
inline std::uint32_t readLong(const Ram& r,unsigned a){return (std::uint32_t(readWord(r,a))<<16)|readWord(r,a+2);}
inline void writeWord(Ram& r,unsigned a,std::uint16_t value){r.at(a)=std::uint8_t(value>>8);r.at(a+1)=std::uint8_t(value);}
inline unsigned geometryAddress(std::uint16_t encoded){return 0x1ef82+std::uint16_t(((encoded<<8)|(encoded>>8))-0xb100);}
inline DrawBridgeInputs readInputs(const Ram& r){return {r[0x1ca33],r[0x1bb1c],r[0x1bb1d],r[0x1bbcd]};}
inline DrawBridgeData readData(const Ram& r){
    DrawBridgeData d;std::copy_n(r.begin()+0x5a996,16,d.angleTable.begin());
    std::copy_n(r.begin()+0x5a9a6,16,d.opponentHeightOffsets.begin());
    d.geometryOffset=readWord(r,0x1f060);d.firstSegmentBase=readWord(r,0x1c6b6);d.secondSegmentBase=readWord(r,0x1c6b8);return d;
}
inline constexpr std::array<unsigned,8> cacheOffsets{{0x06,0x0c,0x4c,0x52,0x14,0x1a,0x3e,0x44}};
inline DrawBridgeState readState(const Ram& r,const DrawBridgeData& d){
    DrawBridgeState s;s.animationPhase=r[0x1bbb0];s.active=r[0x1bb3f];s.updateFlag=r[0x1bb71];s.scratchByte=r[0x1bb1b];
    s.segmentProgressDistance=readWord(r,0x1bc2a);s.stepHeight=readWord(r,0x1bbf6);s.rawTrackDataOffset=readWord(r,0x1bcc0);
    std::copy_n(r.begin()+0x1c985,2,s.rampAngles.begin());std::copy_n(r.begin()+0x1c982,3,s.opponentHeights.begin());
    const unsigned address=geometryAddress(d.geometryOffset);std::copy_n(r.begin()+address,s.geometry.size(),s.geometry.begin());
    if(s.updateFlag){const auto cache=readLong(r,0x7aae6);for(unsigned i=0;i<cacheOffsets.size();++i)s.cachedHeights[i]=readWord(r,cache+cacheOffsets[i]);}
    return s;
}
inline void writeState(Ram& r,const DrawBridgeData& d,const DrawBridgeState& s,const DrawBridgeUpdate& update){
    r[0x1bbb0]=s.animationPhase;r[0x1bb3f]=s.active;
    std::copy(s.opponentHeights.begin(),s.opponentHeights.end(),r.begin()+0x1c982);
    if(!update.geometryUpdated)return;
    r[0x1bb71]=s.updateFlag;r[0x1bb1b]=s.scratchByte;writeWord(r,0x1bc2a,s.segmentProgressDistance);writeWord(r,0x1bbf6,s.stepHeight);writeWord(r,0x1bcc0,s.rawTrackDataOffset);
    std::copy(s.rampAngles.begin(),s.rampAngles.end(),r.begin()+0x1c985);
    std::copy(s.geometry.begin(),s.geometry.end(),r.begin()+geometryAddress(d.geometryOffset));
    if(update.cacheUpdated){const auto cache=readLong(r,0x7aae6);for(unsigned i=0;i<cacheOffsets.size();++i)writeWord(r,cache+cacheOffsets[i],s.cachedHeights[i]);}
}
inline DrawBridgeUpdate animate(Ram& r){
    const auto inputs=readInputs(r);if(inputs.trackId!=5)return {};
    const auto data=readData(r);auto state=readState(r,data);const auto update=animateDrawBridge(data,inputs,state);writeState(r,data,state,update);return update;
}
inline std::vector<unsigned> outputs(const Ram& r){
    std::vector<unsigned> result;const auto range=[&](unsigned first,unsigned count){for(unsigned i=0;i<count;++i)result.push_back(first+i);};
    for(unsigned a:{0x1bbb0U,0x1bb3fU,0x1bb71U,0x1bb1bU})range(a,1);
    for(unsigned a:{0x1bc2aU,0x1bbf6U,0x1bcc0U})range(a,2);
    range(0x1c982,5);range(geometryAddress(readWord(r,0x1f060)),0x48);
    const auto cache=readLong(r,0x7aae6);if(cache&&cache+0x54<=r.size())for(auto offset:cacheOffsets)range(cache+offset,2);
    return result;
}
} // namespace scr::test::drawBridge

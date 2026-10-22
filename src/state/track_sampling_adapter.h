// Shared translation between the recovered state layout and typed native APIs.
// This memory representation has no runtime emulator dependency.
#ifndef SCR_STATE_TRACK_SAMPLING_ADAPTER_H
#define SCR_STATE_TRACK_SAMPLING_ADAPTER_H
#include "track_sampling.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr::state::memory {
using ChipRam=std::array<std::uint8_t,0x80000>;
inline std::uint16_t readWord(const ChipRam& r,unsigned a) { return std::uint16_t((unsigned(r[a])<<8)|r[a+1]); }
inline std::uint32_t readLong(const ChipRam& r,unsigned a) { return (std::uint32_t(readWord(r,a))<<16)|readWord(r,a+2); }
inline void writeWord(ChipRam& r,unsigned a,std::uint16_t v) { r[a]=std::uint8_t(v>>8); r[a+1]=std::uint8_t(v); }
inline void writeLong(ChipRam& r,unsigned a,std::uint32_t v) { writeWord(r,a,std::uint16_t(v>>16)); writeWord(r,a+2,std::uint16_t(v)); }
inline std::int16_t signedWord(std::uint16_t v) { return static_cast<std::int16_t>(v<0x8000 ? int(v) : int(v)-65536); }
inline scr::TrackGeometryData decodeTrackGeometry(const ChipRam& r) {
    scr::TrackGeometryData d; d.segmentCount=r[0x1ca1a];
    for(unsigned i=0;i<100;++i) {
        d.primaryGeometry[i]=r[0x1c4c0+i]; d.alternateGeometry[i]=r[0x1c524+i]; d.properties[i]=r[0x1c5ec+i];
        d.primaryBase[i]=readWord(r,0x1c650+2*i); d.alternateBase[i]=readWord(r,0x1c718+2*i);
    }
    d.geometry.assign(r.begin()+0x1ef82,r.begin()+0x2f082); return d;
}
inline void encodeTrackGeometry(ChipRam& r,const scr::TrackGeometryData& d) {
    r[0x1ca1a]=d.segmentCount;
    for(unsigned i=0;i<100;++i) {
        r[0x1c4c0+i]=d.primaryGeometry[i]; r[0x1c524+i]=d.alternateGeometry[i]; r[0x1c5ec+i]=d.properties[i];
        writeWord(r,0x1c650+2*i,d.primaryBase[i]); writeWord(r,0x1c718+2*i,d.alternateBase[i]);
    }
    for(unsigned i=0;i<d.geometry.size();++i) r[0x1ef82+i]=d.geometry[i];
}
inline scr::TrackSamplingInputs decodeTrackSamplingInputs(const ChipRam& r) {
    return {{{signedWord(readWord(r,0x1bd02)),signedWord(readWord(r,0x1bd08))},
             {signedWord(readWord(r,0x1bd04)),signedWord(readWord(r,0x1bd0a))},
             {signedWord(readWord(r,0x1bd06)),signedWord(readWord(r,0x1bd0c))}},
            readWord(r,0x1bc5e),readWord(r,0x1bb10),r[0x1bb1c],r[0xff],r[0x1bd5c],r[0x1bce4]};
}
inline void encodeTrackSamplingInputs(ChipRam& r,const scr::TrackSamplingInputs& in) {
    const std::array<scr::WheelPoint,3> wheels{in.wheels.frontLeft,in.wheels.frontRight,in.wheels.rearCenter};
    for(unsigned i=0;i<3;++i) { writeWord(r,0x1bd02+2*i,std::uint16_t(wheels[i].x)); writeWord(r,0x1bd08+2*i,std::uint16_t(wheels[i].y)); }
    writeWord(r,0x1bc5e,in.lateralOrigin); writeWord(r,0x1bb10,in.forwardOrigin);
    r[0x1bb1c]=in.playerSegment; r[0xff]=in.clampByteAtFF; r[0x1bd5c]=in.speedHighByte; r[0x1bce4]=in.pitchHighByte;
}
// Exact stores of the recovered selector and its callees; adjacent bytes are
// deliberately absent, including low bytes beside byte-sized fields.

inline scr::TrackSamplingState decodeTrackSamplingState(const ChipRam& r) {
    scr::TrackSamplingState s; auto& c=s.segment;
    c.primaryOffset=readWord(r,0x1bc8c); c.alternateOffset=readWord(r,0x1bc90); c.descriptorOffset=readWord(r,0x1bcbc);
    c.primaryBase=readWord(r,0x1bc0e); c.alternateBase=readWord(r,0x1bc10);
    c.format=r[0x1bb79]; c.alternateFlag=r[0x1bbdc]; c.slopeFlags=r[0x1bc4a]; c.reverseDirection=r[0x1bc32]; c.geometryType=r[0x1bb86];
    c.steeringFlags=r[0x1bb4d]; c.limit=r[0x1bb97]; c.limitDoubled=r[0x1bb59]; c.maximum=r[0x1bb98]; c.maximumDoubled=r[0x1bb5a]; c.mode=r[0x1bb6a];
    c.directionInversion=r[0x1bc44]; c.primaryOrientation=r[0x1bb7b]; c.alternateOrientation=r[0x1bbd9]; c.steeringBase=r[0x1bbd4];
    s.currentSegment=r[0x1bb85]; s.wheelOffset=r[0x1bbf9]; s.dataIndex=r[0x1bba3]; s.lateral=r[0x1bc4d]; s.playerLateral=r[0x1bba1];
    s.forwardPosition=readWord(r,0x1bc40); s.outOfBoundsPosition=readWord(r,0x1bc22);
    for(unsigned i=0;i<4;++i) s.controls[i]=readWord(r,0x1bc02+2*i);
    for(unsigned i=0;i<3;++i) s.heights[i]=readLong(r,0x1bca4+4*i);
    s.sampledHeight=readLong(r,0x1bb18); s.outOfBoundsFlag=r[0x1bb65]; s.renderingOrderMode=r[0x1bb9a]; s.bouncePhase=r[0x1bbda];
    return s;
}
inline void encodeTrackSamplingState(ChipRam& r,const scr::TrackSamplingState& s) {
    const auto& c=s.segment;
    writeWord(r,0x1bc8c,c.primaryOffset); writeWord(r,0x1bc90,c.alternateOffset); writeWord(r,0x1bcbc,c.descriptorOffset);
    writeWord(r,0x1bc0e,c.primaryBase); writeWord(r,0x1bc10,c.alternateBase);
    r[0x1bb79]=c.format; r[0x1bbdc]=c.alternateFlag; r[0x1bc4a]=c.slopeFlags; r[0x1bc32]=c.reverseDirection; r[0x1bb86]=c.geometryType;
    r[0x1bb4d]=c.steeringFlags; r[0x1bb97]=c.limit; r[0x1bb59]=c.limitDoubled; r[0x1bb98]=c.maximum; r[0x1bb5a]=c.maximumDoubled; r[0x1bb6a]=c.mode;
    r[0x1bc44]=c.directionInversion; r[0x1bb7b]=c.primaryOrientation; r[0x1bbd9]=c.alternateOrientation; r[0x1bbd4]=c.steeringBase;
    r[0x1bb85]=s.currentSegment; r[0x1bbf9]=s.wheelOffset; r[0x1bba3]=s.dataIndex; r[0x1bc4d]=s.lateral; r[0x1bba1]=s.playerLateral;
    writeWord(r,0x1bc40,s.forwardPosition); writeWord(r,0x1bc22,s.outOfBoundsPosition);
    for(unsigned i=0;i<4;++i) writeWord(r,0x1bc02+2*i,s.controls[i]);
    for(unsigned i=0;i<3;++i) writeLong(r,0x1bca4+4*i,s.heights[i]);
    writeLong(r,0x1bb18,s.sampledHeight); r[0x1bb65]=s.outOfBoundsFlag; r[0x1bb9a]=s.renderingOrderMode; r[0x1bbda]=s.bouncePhase;
}
} // namespace scr::state::memory
#endif

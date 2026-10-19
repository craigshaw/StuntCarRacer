#include "track_progress.h"
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
int signedWord(std::uint16_t x) { return x<0x8000 ? int(x) : int(x)-65536; }
int signedByte(std::uint8_t x) { return x<0x80 ? int(x) : int(x)-256; }
int floorShift(int x,unsigned n) { const int d=1<<n;return x/d-(x%d<0); }
std::uint16_t word(int x) { return static_cast<std::uint16_t>(x); }
std::uint8_t byte(int x) { return static_cast<std::uint8_t>(x); }
std::uint16_t multiplyByte(std::uint16_t x,std::uint8_t factor) {
    const auto product=static_cast<std::uint32_t>(signedWord(x)*int(factor)*128);
    return std::uint16_t((product<<1)>>16);
}
unsigned geometryOffset(std::uint16_t encoded) {
    return std::uint16_t(((encoded<<8)|(encoded>>8))-0xb100);
}
std::uint8_t geometryByte(const TrackProgressData& d,unsigned a) {
    if(a>=d.track.geometry.size())throw std::out_of_range("Track progress geometry read outside database");
    return d.track.geometry[a];
}
std::uint16_t littleWord(const TrackProgressData& d,unsigned a) {
    return std::uint16_t(geometryByte(d,a)|(unsigned(geometryByte(d,a+1))<<8));
}
void camera(const TrackProgressInputs& in,TrackProgressState& s) {
    s.cameraRotation=std::uint16_t((unsigned(in.yaw)+0x2000)&0xc000);
    s.temporarySegment=std::uint8_t(s.cameraRotation>>8);
    if(!(s.worldPosition[0]&0xffff))++s.worldPosition[0];
    if(!(s.worldPosition[2]&0xffff))++s.worldPosition[2];
    const auto x=s.worldPosition[0]<<1,z=s.worldPosition[2]<<1;
    s.subGridX=std::uint8_t(x>>16);s.viewX=std::uint8_t(x>>24);
    s.subGridZ=std::uint8_t(z>>16);s.viewZ=std::uint8_t(z>>24);
    switch(s.cameraRotation) {
    case 0: s.rotatedX=s.worldPosition[0];s.rotatedZ=s.worldPosition[2];break;
    case 0x4000:s.rotatedX=0x08000000u-s.worldPosition[2];s.rotatedZ=s.worldPosition[0];break;
    case 0x8000:s.rotatedX=0x08000000u-s.worldPosition[0];s.rotatedZ=0x08000000u-s.worldPosition[2];break;
    default:s.rotatedX=s.worldPosition[2];s.rotatedZ=0x08000000u-s.worldPosition[0];break;
    }
    s.carHeight=std::uint16_t(s.worldPosition[1]>>11);
    auto speed=in.averageWheelVelocity;
    speed=speed<0x500 ? word(speed+0x780) : word(speed*2+0x280);
    if(in.pitch&0x8000)speed=word(speed-floorShift(signedWord(in.pitch),1));
    s.cameraHeight=word(floorShift(signedWord(speed),4)+s.carHeight);
    const auto offsetX=word(-int((s.rotatedX>>12)&0x7ff));
    const auto offsetZ=word(-int((s.rotatedZ>>12)&0x7ff));
    s.baseX=std::uint16_t((s.baseX&0xff00)|(offsetX&0xff));s.offsetXHigh=std::uint8_t(offsetX>>8);
    s.baseZ=std::uint16_t((s.baseZ&0xff00)|(offsetZ&0xff));s.offsetZHigh=std::uint8_t(offsetZ>>8);
    s.cameraYaw=word(int((unsigned(in.yaw)+0x2000)&0x3ffe)-0x2000);
}
std::uint8_t findSegment(const TrackProgressData& d,TrackProgressState& s) {
    s.gridLookupX=s.gridLookupZ=0;
    if(s.viewZ>=16)return 0xff;
    s.scratch4=byte(s.viewZ*16);
    if(s.viewX>=16)return 0xff;
    auto index=byte(s.viewX|s.scratch4);
    auto segment=d.grid[index];
    if(segment!=0xff)return segment;
    s.scratch4=s.subGridX;s.scratch3=s.subGridZ;
    const bool carry=unsigned(s.subGridX)+s.subGridZ>255;
    if(s.subGridZ<s.subGridX) {
        if(carry) { if((index&15)==15)return 0xff;++index; }
        else { if(!(index&0xf0))return 0xff;index=byte(index-16); }
    } else {
        if(carry) { if((index&0xf0)==0xf0)return 0xff;index=byte(index+16); }
        else { if(!(index&15))return 0xff;--index; }
    }
    return d.grid[index];
}
void segmentToView(const TrackProgressData& d,TrackProgressState& s) {
    const auto coordinate=d.segmentCoordinates[s.currentSegment];
    auto x=byte((coordinate&15)-s.viewX),z=byte((coordinate>>4)-s.viewZ);
    switch(s.cameraRotation) {
    case 0x4000:{const auto old=x;x=byte(-z);z=old;break;}
    case 0x8000:x=byte(-x);z=byte(-z);break;
    case 0xc000:{const auto old=x;x=z;z=byte(-old);break;}
    default:break;
    }
    s.transformedGridX=x;s.transformedGridZ=z;
    s.baseX=std::uint16_t((unsigned(byte(x*8+s.offsetXHigh))<<8)|(s.baseX&255));
    s.baseZ=std::uint16_t((unsigned(byte(z*8+s.offsetZHigh))<<8)|(s.baseZ&255));
}
void directionTransform(TrackProgressState& s) {
    switch(s.trackRotation&0xc000) {
    case 0:s.temporaryX=word(-s.baseX);s.temporaryZ=word(-s.baseZ);break;
    case 0x4000:s.temporaryX=word(-s.baseZ);s.temporaryZ=word(0x800+s.baseX);break;
    case 0x8000:s.temporaryX=word(0x800+s.baseX);s.temporaryZ=word(0x800+s.baseZ);break;
    default:s.temporaryX=word(0x800+s.baseZ);s.temporaryZ=word(-s.baseX);break;
    }
}
void curveCoordinates(const TrackProgressData& d,unsigned base,TrackProgressState& s) {
    const auto x=littleWord(d,base+2),z=littleWord(d,base+4);
    switch(s.trackRotation&0xc000) {
    case 0:s.temporaryX=word(s.baseX+x);s.temporaryZ=word(s.baseZ+z);break;
    case 0x4000:s.temporaryX=word(s.baseX-z+0x800);s.temporaryZ=word(s.baseZ+x);break;
    case 0x8000:s.temporaryX=word(s.baseX-x+0x800);s.temporaryZ=word(s.baseZ-z+0x800);break;
    default:s.temporaryX=word(s.baseX+z);s.temporaryZ=word(s.baseZ-x+0x800);break;
    }
}
struct Polar { std::uint16_t angle,magnitude; };
Polar polar(const TrackProgressData& d,std::uint16_t x,std::uint16_t z) {
    const auto ax=word(x&0x8000 ? -int(x) : x),az=word(z&0x8000 ? -int(z) : z);
    // The game's nearest-cell geometry keeps both magnitudes below 0x8000.
    // At the signed-word minimum its DIVU/register-overflow path is outside
    // that calling domain and cannot be represented by a geometric ratio.
    if(ax==0x8000||az==0x8000)throw std::out_of_range("Track polar coordinate outside valid signed magnitude domain");
    std::uint16_t angle,ratio;
    if(ax==az){ratio=0xffff;angle=0x2000;}
    else if(az<ax) {
        ratio=std::uint16_t((std::uint32_t(az)<<16)/ax);
        angle=d.angleTable[ratio>>5];
        if(!((x^z)&0x8000))angle=word(-angle);
        angle=word(angle+((x&0x8000)?0xc000:0x4000));
        const auto magnitude=word((std::uint32_t(d.magnitudeTable[ratio>>5])*std::min(ax,az)>>16)+std::max(ax,az));
        return {angle,magnitude};
    } else {ratio=std::uint16_t((std::uint32_t(ax)<<16)/az);angle=d.angleTable[ratio>>5];}
    if((x^z)&0x8000)angle=word(-angle);
    if(z&0x8000)angle=word(angle+0x8000);
    const auto magnitude=word((std::uint32_t(d.magnitudeTable[ratio>>5])*std::min(ax,az)>>16)+std::max(ax,az));
    return {angle,magnitude};
}
void perspective(const TrackProgressData& d,TrackProgressState& s) {
    auto square=[](std::uint16_t v){const int x=signedWord(v);return std::uint32_t(x*x);};
    const auto difference=square(s.temporaryX)+square(s.temporaryZ)-square(s.radialDistance);
    if(s.segment.geometryType>=d.perspectiveFactors.size())throw std::out_of_range("Curved geometry type outside perspective table");
    s.scratch3=d.perspectiveFactors[s.segment.geometryType];
    const auto correction=multiplyByte(std::uint16_t(difference>>8),s.scratch3);
    s.radialDistance=word(s.radialDistance+floorShift(signedWord(correction),4));
}
void geometry(const TrackProgressData& d,const TrackProgressInputs& in,TrackProgressState& s) {
    for(unsigned iteration=0;iteration<2;++iteration) {
        s.segment=loadTrackSegmentConfiguration(d.track,s.currentSegment);
        const auto base=geometryOffset(s.segment.descriptorOffset);
        segmentToView(d,s);
        const auto slope=std::uint16_t((unsigned(s.segment.slopeFlags)<<8)|in.slopeLowByte);
        s.trackRotation=word(s.cameraRotation-slope);
        if(!(s.segment.steeringFlags&0x80)) {
            directionTransform(s);
            if(!(s.segment.steeringFlags&0x40)) {
                s.lateralOrigin=word(s.temporaryX-littleWord(d,base+2));
                s.forwardOrigin=s.temporaryZ;s.targetHeading=slope;return;
            }
            s.scratch3=0xb5;
            s.lateralOrigin=word(multiplyByte(word(s.temporaryX-s.temporaryZ),s.scratch3)-littleWord(d,base+2));
            s.scratch3=geometryByte(d,base+7);
            s.forwardOrigin=multiplyByte(word(s.temporaryX+s.temporaryZ),s.scratch3);
            s.targetHeading=word(littleWord(d,base+4)+slope);return;
        }
        curveCoordinates(d,base,s);
        const auto p=polar(d,s.temporaryX,s.temporaryZ);s.radialDistance=p.magnitude;
        s.radialAngle=word(p.angle+s.cameraRotation+0x8000);
        const auto inversion=std::uint16_t((unsigned(s.segment.directionInversion)<<8)|in.inversionLowByte);
        s.targetHeading=word(s.radialAngle+0x4000-inversion);
        auto distance=word(s.radialAngle-word(littleWord(d,base+6)<<6)-slope);
        if(distance&0x8000)distance=word(-distance);
        s.scratch3=geometryByte(d,base+8);distance=multiplyByte(distance,s.scratch3);
        const int shift=1-int(s.segment.steeringFlags&3);
        distance=shift<0 ? word(unsigned(distance)<<unsigned(-shift)) : std::uint16_t(distance>>shift);
        s.forwardOrigin=distance;
        const auto sampleIndex=byte((distance>>7)+2);
        if(signedByte(sampleIndex)>=signedByte(s.segment.limit)&&(s.segment.geometryType==1||s.segment.geometryType==3)) {
            s.temporarySegment=s.currentSegment;
            if(s.segment.reverseDirection)s.currentSegment=s.currentSegment ? byte(s.currentSegment-1) : byte(d.track.segmentCount-1);
            else s.currentSegment=byte((s.currentSegment+1)%d.track.segmentCount);
            if((d.track.properties[s.currentSegment]&15)==4)continue;
            s.currentSegment=s.temporarySegment;
        }
        perspective(d,s);
        s.lateralOrigin=word(littleWord(d,base+9)-s.radialDistance);
        if(s.segment.directionInversion&0x80)s.lateralOrigin=word(-s.lateralOrigin);
        return;
    }
    throw std::invalid_argument("Track geometry continuation did not terminate");
}
void boundaries(TrackProgressState& s) {
    s.offRoadSide=0;
    s.lateralOffset=word(s.lateralOrigin-0xc0);
    if(s.segment.reverseDirection&0x80)s.lateralOffset=word(-s.lateralOffset);
    const auto distance=word(s.lateralOffset&0x8000 ? -int(s.lateralOffset) : s.lateralOffset);
    if(signedWord(distance)>=0xc0)s.offRoadSide=(s.lateralOffset&0x8000) ? 0x80 : 2;
    if(signedWord(distance)>=0x100) {
        if(!(s.offTrackFlags&0x80)) {
            s.offTrackFlags=0x80;s.trackSide=std::uint8_t(s.lateralOffset>>8);s.restartCountdown=0x10;s.debrisY.fill(0xd4);
        }
    } else if(!(s.offTrackFlags&0x40)) {s.offTrackFlags=0;s.raceCompletionCheck=0;}
}
void progress(TrackProgressState& s) {
    const auto length=std::uint16_t(unsigned(s.segment.mode)<<8);
    s.progression=(s.segment.reverseDirection&0x80) ? word(length-s.forwardOrigin) : s.forwardOrigin;
    auto offset=word(s.progression+0x40);s.progressOffset=offset;
    if(signedWord(offset)>=signedWord(length)){s.progressOffset=std::uint16_t((offset&255)|0x8000);offset=0;}
    s.legacyRenderIndex=byte(((offset>>8)+1)*2);s.renderIndex=byte(s.legacyRenderIndex*2);
    const auto distance=(s.segment.reverseDirection&0x80) ? std::uint8_t(word(length-s.forwardOrigin)>>8) : std::uint8_t(s.forwardOrigin>>8);
    s.depth=byte(0x20-distance+((s.progressOffset&0x8000) ? s.segment.mode : 0));s.visibility=s.depth;
}
} // namespace

bool updateTrackProgress(const TrackProgressData& data,const TrackProgressInputs& in,TrackProgressState& s) {
    if(!data.track.segmentCount||data.track.segmentCount>100)throw std::invalid_argument("Track progress requires 1..100 loaded segments");
    camera(in,s);
    const auto found=findSegment(data,s);
    if(found==0xff) {
        s.offTrackFlags=0xc0;
        s.progressOffset=std::uint16_t((unsigned(s.previousProgress)<<8)|(s.progressOffset&255));
        s.visibility=s.previousVisibility;s.depth=s.previousDepth;
        return false;
    }
    if(found>=data.track.segmentCount)throw std::out_of_range("Track grid references an unloaded segment");
    s.currentSegment=found;geometry(data,in,s);boundaries(s);s.playerSegment=s.currentSegment;
    if(!(s.offTrackFlags&0x40))s.lastValidSegment=s.playerSegment;
    progress(s);s.previousProgress=std::uint8_t(s.progressOffset>>8);s.previousVisibility=s.visibility;s.previousDepth=s.depth;
    return true;
}
} // namespace scr

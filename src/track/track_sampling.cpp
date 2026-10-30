#include "track/track_sampling.h"
#include "track/track_interpolation.h"
#include "physics/wheel_contact.h"
#include <stdexcept>

namespace scr {
namespace {
int signedWord(std::uint16_t x) { return x<0x8000 ? int(x) : int(x)-65536; }
int signedByte(std::uint8_t x) { return x<0x80 ? int(x) : int(x)-256; }
int floorShift(int x,unsigned bits) {
    const int scale=1<<bits;
    return x>=0 ? x/scale : -((-x+scale-1)/scale);
}
unsigned offset(std::uint16_t encoded) {
    return std::uint16_t(std::uint16_t((encoded<<8)|(encoded>>8))-0xb100);
}
std::uint8_t byte(const TrackGeometryData& d,unsigned index) {
    if(index>=d.geometry.size()) throw std::out_of_range("Track geometry read outside supplied database");
    return d.geometry[index];
}
std::uint16_t word(const TrackGeometryData& d,unsigned index) {
    return std::uint16_t((unsigned(byte(d,index))<<8)|byte(d,index+1));
}
TrackSegmentConfiguration configuration(const TrackGeometryData& d,std::uint8_t index) {
    if(index>=d.segmentCount) throw std::out_of_range("Track segment outside loaded track");
    TrackSegmentConfiguration c;
    c.format=d.primaryGeometry[index];
    c.primaryOffset=word(d,32+std::uint8_t(c.format*2));
    c.alternateOffset=word(d,32+std::uint8_t(d.alternateGeometry[index]*2));
    c.alternateFlag=(d.alternateGeometry[index]&0x80) ? 2 : 0;
    c.primaryBase=d.primaryBase[index]; c.alternateBase=d.alternateBase[index];
    c.slopeFlags=d.properties[index]&0xc0;
    c.reverseDirection=std::uint8_t((d.properties[index]&0x10)<<3);
    c.geometryType=d.properties[index]&15;
    c.descriptorOffset=word(d,c.geometryType*2);
    const unsigned base=offset(c.descriptorOffset);
    c.steeringFlags=byte(d,base+1);
    auto i=byte(d,base);
    c.limit=byte(d,base+i); i=std::uint8_t(i+1);
    c.limitDoubled=std::uint8_t(c.limit*2);
    c.maximum=std::uint8_t(c.limit-2); c.maximumDoubled=std::uint8_t(c.maximum*2);
    c.mode=std::uint8_t((c.limit>>1)-1);
    c.directionInversion=(byte(d,base+i)&1) ? 0x80 : 0; i=std::uint8_t(i+1);
    c.primaryOrientation=byte(d,base+i); i=std::uint8_t(i+1);
    c.alternateOrientation=byte(d,base+i); i=std::uint8_t(i+3);
    c.steeringBase=byte(d,base+i);
    return c;
}
std::uint16_t control(const TrackGeometryData& d,const TrackSegmentConfiguration& c,std::uint8_t index) {
    const bool alternate=(index&1)!=0;
    const unsigned base=offset(alternate ? c.alternateOffset : c.primaryOffset);
    std::uint16_t value;
    if(c.format&0x80) {
        const unsigned i=index&0xfe;
        value=std::uint16_t((unsigned(byte(d,base+i)&0x7f)<<8)|byte(d,base+i+1));
    } else {
        const unsigned b=byte(d,base+(index>>1));
        value=std::uint16_t(((b&15)<<8)|((b<<1)&0xe0));
    }
    value=std::uint16_t(value+(alternate ? c.alternateBase : c.primaryBase));
    return std::uint16_t(floorShift(signedWord(value),5));
}
std::uint8_t flipFraction(std::uint8_t fraction) {
    const auto result=std::uint8_t(0-fraction);
    return result ? result : 0xff;
}
void transition(const TrackGeometryData& d,TrackSamplingState& s) {
    const bool before=(s.forwardPosition&0x8000)!=0;
    const bool retreat=before!=((s.segment.reverseDirection&0x80)!=0);
    if(retreat) s.currentSegment=s.currentSegment ? std::uint8_t(s.currentSegment-1) : std::uint8_t(d.segmentCount-1);
    else s.currentSegment=std::uint8_t((s.currentSegment+1)%d.segmentCount);
    s.segment=configuration(d,s.currentSegment);
    const bool end=retreat!=((s.segment.reverseDirection&0x80)!=0);
    s.dataIndex=end ? std::uint8_t(s.segment.limit-4) : 0;
    if(before!=end) {
        s.forwardPosition=std::uint16_t((s.forwardPosition&0xff00)|flipFraction(std::uint8_t(s.forwardPosition)));
        s.lateral=flipFraction(s.lateral);
    }
}
std::uint16_t scaledCoordinate(int coordinate,unsigned shift,std::uint8_t orientation) {
    // MULS followed by ASL.L #1 / SWAP exposes the product's signed high word.
    const auto product=std::uint32_t(floorShift(coordinate,shift)*int(orientation)*128);
    return std::uint16_t((product<<1)>>16);
}
} // namespace

TrackSegmentConfiguration loadTrackSegmentConfiguration(const TrackGeometryData& data,std::uint8_t segmentIndex) {
    if(data.segmentCount==0 || data.segmentCount>100)
        throw std::invalid_argument("Track segment count must be in [1,100]");
    return configuration(data,segmentIndex);
}

std::array<WheelTrackSample,3> sampleTrackWheelHeights(
    const TrackGeometryData& data,const TrackSamplingInputs& in,TrackSamplingState& s) {
    if(data.segmentCount==0 || data.segmentCount>100)
        throw std::invalid_argument("Track segment count must be in [1,100]");
    if(in.playerSegment>=data.segmentCount)
        throw std::out_of_range("Player segment outside loaded track");
    s.currentSegment=in.playerSegment; s.segment=configuration(data,s.currentSegment);
    s.renderingOrderMode=0;
    const std::array<WheelPoint,3> points{in.wheels.frontLeft,in.wheels.frontRight,in.wheels.rearCenter};
    std::array<WheelTrackSample,3> events{};
    for(unsigned n=0;n<3;++n) {
        const unsigned wheel=2-n;
        s.wheelOffset=std::uint8_t(wheel*2);
        if(s.currentSegment!=in.playerSegment) {
            s.currentSegment=in.playerSegment; s.segment=configuration(data,s.currentSegment);
        }
        auto lateral=std::uint16_t(floorShift(points[wheel].x,4)+in.lateralOrigin);
        if(lateral>=0x180) {
            s.outOfBoundsFlag|=0x80; s.outOfBoundsPosition=lateral;
            s.lateral=signedWord(lateral)<0 ? 0 : 0xff;
        } else {
            const auto product=std::uint32_t(signedWord(lateral)*int(s.segment.primaryOrientation)*128);
            lateral=std::uint16_t((product<<1)>>16);
            // This is an absolute byte read in the ORIGINAL binary, not #$ff.
            s.lateral=signedWord(lateral)>=256 ? in.clampByteAtFF : std::uint8_t(lateral);
        }
        if(wheel==2) s.playerLateral=std::uint8_t(s.lateral^((s.segment.reverseDirection&0x80) ? 0xff : 0));
        s.forwardPosition=std::uint16_t(scaledCoordinate(points[wheel].y,3,s.segment.alternateOrientation)+in.forwardOrigin);
        s.dataIndex=std::uint8_t((s.forwardPosition>>8)*2);
        if(signedByte(s.dataIndex)<0 || signedByte(s.dataIndex)>=signedByte(s.segment.maximum)) transition(data,s);
        auto index=s.dataIndex;
        if(s.segment.reverseDirection&0x80) index=std::uint8_t(s.segment.limit-index-4);
        for(unsigned c=0;c<4;++c) s.controls[(s.segment.reverseDirection&0x80) ? 3-c : c]=control(data,s.segment,std::uint8_t(index+c));
        auto& event=events[n];
        event.wheelIndex=std::uint8_t(wheel); event.segmentIndex=s.currentSegment;
        event.lateral=s.lateral; event.blend=std::uint8_t(s.forwardPosition);
        event.reverseDirection=s.segment.reverseDirection;
        event.outOfBoundsFlag=s.outOfBoundsFlag; event.outOfBoundsPosition=s.outOfBoundsPosition;
        event.controls=s.controls;
        event.rawHeight=interpolateTrackCoordinate({s.controls[0],s.controls[1],s.controls[2],s.controls[3],s.lateral,event.blend});
        WheelContactState contact{s.heights[wheel],s.sampledHeight,s.outOfBoundsFlag,s.renderingOrderMode,s.bouncePhase};
        applyWheelHeightContact(event.rawHeight,{s.outOfBoundsPosition,s.segment.reverseDirection,in.speedHighByte,in.pitchHighByte},contact);
        s.heights[wheel]=contact.height; s.sampledHeight=contact.sampledHeight;
        s.outOfBoundsFlag=contact.outOfBoundsFlag; s.renderingOrderMode=contact.renderingOrderMode; s.bouncePhase=contact.bouncePhase;
        event.contactHeight=contact.height; event.sampledHeight=contact.sampledHeight;
    }
    return events;
}
} // namespace scr

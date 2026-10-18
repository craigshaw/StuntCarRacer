#include "steering_response.h"

namespace scr {
namespace {
int signedWord(std::uint16_t x) {return x<0x8000 ? int(x) : int(x)-65536;}
std::uint16_t negate(std::uint16_t x) {return std::uint16_t(0-x);}
std::uint16_t scaleByByte(std::uint16_t value,std::uint8_t factor) {
    // Original MULS by (factor <<7), ASL.L #1, SWAP: signed value*factor/256.
    const auto product=std::uint32_t(signedWord(value)*int(factor)*128);
    return std::uint16_t((product<<1)>>16);
}
std::uint16_t shiftRightThree(std::uint16_t x) {
    const int v=signedWord(x);return std::uint16_t(v>=0 ? v/8 : -((-v+7)/8));
}
void correctYaw(const SteeringResponseInputs& in,SteeringResponseState& s) {
    s.steeringDirection=std::uint8_t(s.angleDeltaScratch>>8);
    auto correction=s.progressDistance;
    auto factor=std::uint8_t(correction);
    bool scale=true;
    if(correction>>8) {
        correction=std::uint16_t(correction-0x1e00);
        if(signedWord(correction)>=0) scale=false;
        else factor=0xff;
    }
    if(scale) {
        s.scratch3=factor;
        auto speed=in.forwardVelocity;
        if(signedWord(speed)<0)speed=negate(speed);
        speed=std::uint16_t(speed+0x0a00);
        if(signedWord(speed)<0)speed=0x7f00;
        correction=std::uint16_t(scaleByByte(speed,factor)>>7); // LSR.W, not ASR.
        if((correction&0xff)==0)correction=std::uint16_t((correction&0xff00)|1);
    }
    if(s.angleDeltaScratch&0x8000)correction=negate(correction);
    s.yawAngle=std::uint16_t(s.yawAngle+correction);
}
} // namespace

void calculateSteeringResponse(const TrackGeometryData& data,const SteeringOffsetTable& offsets,
                              const SteeringResponseInputs& in,SteeringResponseState& s) {
    s.currentSegment=in.playerSegment;s.segment=loadTrackSegmentConfiguration(data,s.currentSegment);
    const auto reverseWord=std::uint16_t((unsigned(s.segment.reverseDirection)<<8)|in.reverseDirectionLowByte);
    auto delta=std::uint16_t(in.targetAngle-s.yawAngle);
    delta^=reverseWord;
    unsigned offsetIndex=0;
    if(s.segment.steeringFlags&0x80)
        offsetIndex=((s.segment.directionInversion^s.segment.reverseDirection)&0x80) ? 2 : 1;
    delta=std::uint16_t(delta+offsets[offsetIndex]);
    s.angleDeltaScratch=delta;
    s.progressDistance=(delta&0x8000) ? negate(delta) : delta;
    s.steeringScale=s.progressDistance>=0x800 ? 0x7fff : std::uint16_t(s.progressDistance<<4);
    if(std::uint8_t(s.segment.mode-in.trackProgression)<2) {
        s.currentSegment=std::uint8_t((s.currentSegment+1)%data.segmentCount);
        s.segment=loadTrackSegmentConfiguration(data,s.currentSegment);
    }
    s.directionMultiplier=std::uint8_t(s.segment.directionInversion^s.segment.reverseDirection);
    std::uint16_t acceleration=0;
    bool useSteeringForce=false;
    std::uint8_t steeringFactor=0;
    if(s.steeringDirection) {
        s.scratch4=std::uint8_t(s.steeringDirection^(s.angleDeltaScratch>>8));
        steeringFactor=s.segment.steeringBase;
        bool addScale=true;
        if(s.segment.steeringFlags&0x80) {
            if((s.steeringDirection^s.directionMultiplier)&0x80) {
                s.steeringDirection=s.directionMultiplier;
                steeringFactor=std::uint8_t(steeringFactor-0x23);
                addScale=false;
            } else steeringFactor=std::uint8_t(steeringFactor+0x2d);
        }
        if(addScale && !(s.scratch4&0x80))steeringFactor=std::uint8_t(steeringFactor+(s.steeringScale>>8));
        useSteeringForce=true;
    } else if(s.segment.steeringFlags&0x80) {
        s.steeringDirection=s.directionMultiplier;
        steeringFactor=s.segment.steeringBase;useSteeringForce=true;
    }
    if(useSteeringForce) {
        s.scratch3=steeringFactor;
        auto force=scaleByByte(in.forwardVelocity,steeringFactor);
        if(s.steeringDirection&0x80)force=negate(force);
        acceleration=shiftRightThree(force);
        if((s.progressDistance>>8)>=0x1e)correctYaw(in,s);
    } else correctYaw(in,s);
    acceleration=std::uint16_t(acceleration-in.yawVelocity);
    s.yawAcceleration=(in.integrityWord==0x9cedcd02u && in.wheelMovementActive) ? acceleration : 0;
}
} // namespace scr

#include "race/opponent_driving.h"
#include "track/track_interpolation.h"
#include <stdexcept>

namespace scr {
namespace {
int sw(std::uint16_t x){return x<0x8000?int(x):int(x)-65536;}
int sb(std::uint8_t x){return x<0x80?int(x):int(x)-256;}
int shr(int x,unsigned n){const int d=1<<n;return x/d-(x%d<0);}
std::uint16_t w(int x){return static_cast<std::uint16_t>(x);}
std::uint8_t b(int x){return static_cast<std::uint8_t>(x);}
std::uint16_t half(std::uint16_t x){return w(shr(sw(x),1));}
std::uint16_t timestep(std::uint16_t x){return w(shr(sw(x)*238,8));}
std::uint8_t random(OpponentDrivingState& s){const auto v=b(int((std::uint16_t(s.randomSeed)>>4)^((s.randomSeed>>16)>>1)));s.randomSeed=(s.randomSeed<<8)|s.randomTail;s.randomTail=v;return v;}
void validate(const OpponentDrivingData& d,const OpponentDrivingInputs& in,const OpponentDrivingState& s){if(in.opponentId>=d.traits.size()||!d.track.track.segmentCount||s.segment>=d.track.track.segmentCount)throw std::out_of_range("Opponent driving data outside original valid domain");}
void average(OpponentDrivingState& s,unsigned a,unsigned c){const auto v=half(w(s.velocity[a]+s.velocity[c]));s.velocity[a]=s.velocity[c]=v;}
int suspension(OpponentDrivingState& s,const OpponentDrivingData& d,unsigned a,unsigned c,std::uint16_t limit){
    s.progress.temporaryX=limit;s.progress.temporaryZ=std::uint16_t((s.progress.temporaryZ&0xff00)|a*2);
    const auto difference=w(s.wheelPosition[a]-s.wheelPosition[c]);const auto magnitude=w((difference&0x8000)?-int(difference):difference);
    const auto excess=w(limit-magnitude);
    if(excess&0x8000){const unsigned selected=(difference&0x8000)?c:a;s.wheelPosition[selected]=w(s.wheelPosition[selected]+excess);
        if(c!=2){average(s,0,c);return sw(difference);}
        average(s,0,1);average(s,2,1);average(s,0,1);
    }
    if(c==2&&!s.enabled){const auto delta=w(s.velocity[a]-s.velocity[2]);if((delta&0x8000)||sw(delta)<16)for(unsigned i=0;i<3;++i)s.velocity[i]=w(s.velocity[i]+d.suspensionDamping[i]);}
    return sw(difference);
}
void footprint(OpponentDrivingState& s){
    s.footprint[0]=w(s.wheelPosition[0]+0x50);s.footprint[2]=w(s.wheelPosition[1]+0x50);s.footprint[1]=w(s.wheelPosition[2]+0x50);
    const auto delta=half(w(s.footprint[2]-s.footprint[0]));s.footprint[3]=w(s.footprint[1]+delta);s.footprint[1]=w(s.footprint[1]-delta);
}
void wheels(const OpponentDrivingData& d,const OpponentDrivingInputs& in,OpponentDrivingState& s){
    s.minimumBoundary=0x8000;const auto resting=std::uint16_t((s.progress.segment.steeringFlags&0x80)?0x7c:0x28);s.progress.temporaryZ=resting;unsigned enabled=0;
    for(unsigned i=0;i<3;++i){const auto distance=w(s.bounds[i]-s.wheelPosition[i]);if(sw(distance)>=sw(s.minimumBoundary))s.minimumBoundary=distance;
        auto compression=w(distance+resting);if((compression&0x8000)&&compression<0xffa0)compression=0xffa0;
        auto force=w(shr(sw(w(compression-s.previousHeight[i]))*0x114,8)+compression);if(force&0x8000)force=0;if(sw(force)>=0x400)force=0x3ff;
        enabled|=force;s.acceleration[i]=w(force-resting);s.previousHeight[i]=compression;
    }
    s.enabled=b(int((enabled>>8)|enabled));const auto total=w(s.acceleration[0]+s.acceleration[1]+s.acceleration[2]);
    for(unsigned i=0;i<3;++i)s.wheelForce[i]=w(shr(sw(w(total+s.acceleration[i]+w(s.acceleration[i]*4))),3));
    if((d.traits[in.opponentId]&4)&&!((s.velocity[2]|s.wheelForce[2])&0xfffc)&&!(random(s)&15))s.velocity[2]=0xa0;
    for(unsigned i=0;i<3;++i){s.velocity[i]=w(s.velocity[i]+timestep(s.wheelForce[i]));s.wheelPosition[i]=w(s.wheelPosition[i]+half(timestep(s.velocity[i])));}
    const auto difference=suspension(s,d,0,1,0x128);(void)suspension(s,d,difference<0?1:0,2,0x170);footprint(s);
}
void ai(const OpponentDrivingData& d,const OpponentDrivingInputs& in,OpponentDrivingState& s){
    if(!s.enabled||in.network)return;s.segmentOffsets={0,0};s.movementOverride=0;
    const auto timer=s.actionTimer;
    if(timer){if(!(in.frameThrottle&0x80))--s.actionTimer;const unsigned index=(timer+s.patternOffset)&15;const auto value=d.patterns[index];s.segmentOffsets[(value&0x80)?1:0]=b((value&0x80)?-int(value):value);s.movementOverride=d.patterns[(index+5)&15];}
    else if(!(d.visibility[s.segment]&0x80)&&!(s.ahead&0x80)&&!(s.progress.segment.steeringFlags&0x80)&&(s.patternControl&0x80)){
        s.patternOffset=(s.patternControl&0x40)?16:8;s.progress.scratch4=b(random(s)&31);if(sb(in.opponentId)>=sb(s.progress.scratch4))s.actionTimer=16;
    }
    s.patternControl=std::uint8_t((s.progress.segment.reverseDirection>>1)^s.progress.segment.steeringFlags);
}
void target(const OpponentDrivingInputs& in,OpponentDrivingState& s){const auto high=s.enabled?b(in.baseSpeedHigh-(s.actionTimer?25:0)):0;const auto low=s.enabled?in.baseSpeedLow:0;s.targetSpeed=std::uint16_t((unsigned(low)<<8)|unsigned(high));}
void avoidance(const OpponentDrivingData& d,OpponentDrivingState& s){
    if(!s.enabled)return;auto limit=d.visibility[s.segment];if(!(limit&0x80)&&limit>=d.lookahead)limit=d.lookahead;s.collisionDistance=limit&127;
    const auto speed=std::uint8_t(s.speed>>8);const auto delta=b(speed-s.collisionDistance);
    if(speed>=s.collisionDistance){s.boundaryCollision=0x80;if(!delta)return;s.targetSpeed=w(-int(s.targetSpeed));if(delta<14)return;}
    if(!(d.visibility[s.segment]&0x80)&&(delta&0x80)&&s.boundaryCollision){if(delta>=0xfe)return;s.boundaryCollision&=0x7f;}
    s.targetSpeed=w(s.targetSpeed*2);
}
void acceleration(OpponentDrivingState& s){
    s.scratch1=s.scratch2=0;
    const auto speed=std::uint8_t(s.speed>>8);
    if(!(speed&0x80)){
        auto multiplier=b((unsigned(speed)<<1)|((s.speed&0x80)?1:0));
        if((s.drafting&0x80)&&(s.ahead&0x80))multiplier=multiplier<20?0:b(multiplier-20);
        s.progress.scratch3=multiplier;const unsigned product=unsigned(speed)*multiplier;s.progress.scratch4=b(int(product));const auto drag=std::uint16_t(product>>6);s.scratch1=std::uint8_t(drag>>8);s.scratch2=std::uint8_t(drag);
        if(s.enabled&&!(s.targetSpeed&0x8000)){s.targetSpeed=w(s.targetSpeed-speed);if(s.progress.segment.steeringFlags&0x80)s.targetSpeed=w(s.targetSpeed-speed-0x23);}
    }
    auto force=w(s.targetSpeed-((unsigned(s.scratch1)<<8)|s.scratch2));
    if(s.enabled){const auto gradient=w(((s.bounds[0]+s.bounds[1])&0xffffu)/2-s.bounds[2]);auto magnitude=w((gradient&0x8000)?-int(gradient):gradient);if(magnitude>=0x200)magnitude=0x1fe;magnitude=std::uint16_t(magnitude>>1);const auto contribution=w(magnitude+shr(sw(magnitude),2));force=w(force+((gradient&0x8000)?-int(contribution):int(contribution)));}
    s.speed=w(s.speed+timestep(force));if(s.speed&0x8000)s.speed=0;
}
unsigned offset(std::uint16_t encoded){return std::uint16_t(((encoded<<8)|(encoded>>8))-0xb100);}
std::uint8_t read(const OpponentDrivingData& d,unsigned i){if(i>=d.track.track.geometry.size())throw std::out_of_range("Opponent geometry outside supplied database");return d.track.track.geometry[i];}
std::uint16_t little(const OpponentDrivingData& d,unsigned i){return std::uint16_t(read(d,i)|(unsigned(read(d,i+1))<<8));}
void config(const OpponentDrivingData& d,OpponentDrivingState& s){s.progress.segment=loadTrackSegmentConfiguration(d.track.track,s.progress.currentSegment);}
void previous(const OpponentDrivingData& d,OpponentDrivingState& s){s.progress.currentSegment=s.progress.currentSegment?b(s.progress.currentSegment-1):b(d.track.track.segmentCount-1);}
void next(const OpponentDrivingData& d,OpponentDrivingState& s){s.progress.currentSegment=b((s.progress.currentSegment+1)%d.track.track.segmentCount);}
std::uint16_t behind(const OpponentDrivingData& d,OpponentDrivingState& s,unsigned distance){
    s.progress.currentSegment=s.segment;auto value=w(s.subSegment-int(distance));if(value&0x8000){previous(d,s);config(d,s);value=w((s.progress.segment.mode<<8)+value);}s.behindProgress=value;s.behindSegment=s.progress.currentSegment;return value;
}
std::uint16_t control(const OpponentDrivingData& d,OpponentDrivingState& s,std::uint8_t index){const auto& c=s.progress.segment;const bool alternate=index&1;const auto base=offset(alternate?c.alternateOffset:c.primaryOffset);std::uint16_t value;
    if(c.format&0x80)value=std::uint16_t((unsigned(read(d,base+(index&0xfe))&127)<<8)|read(d,base+(index&0xfe)+1));
    else{const auto v=read(d,base+(index>>1));value=std::uint16_t(((v&15)<<8)|((v<<1)&0xe0));}
    return w(shr(sw(w(value+(alternate?c.alternateBase:c.primaryBase))),5));
}
void segmentBase(const OpponentDrivingData& d,OpponentDrivingState& s){auto& p=s.progress;const auto coordinate=d.track.segmentCoordinates[p.currentSegment];auto x=b((coordinate&15)-p.viewX),z=b((coordinate>>4)-p.viewZ);
    switch(p.cameraRotation){case 0x4000:{const auto old=x;x=b(-z);z=old;break;}case 0x8000:x=b(-x);z=b(-z);break;case 0xc000:{const auto old=x;x=z;z=b(-old);break;}default:break;}
    p.transformedGridX=x;p.transformedGridZ=z;p.baseX=std::uint16_t((unsigned(b(x*8+p.offsetXHigh))<<8)|(p.baseX&255));p.baseZ=std::uint16_t((unsigned(b(z*8+p.offsetZHigh))<<8)|(p.baseZ&255));
}
std::array<std::uint16_t,2> coordinate(const OpponentDrivingData& d,OpponentDrivingState& s,std::uint8_t i){const auto base=offset(s.progress.segment.descriptorOffset);const auto x=little(d,base+i),z=little(d,base+b(i+2));auto& p=s.progress;
    switch(s.heightDifference&0xc000){case 0:p.temporaryX=w(p.baseX+x);p.temporaryZ=w(p.baseZ+z);break;case 0x4000:p.temporaryX=w(p.baseX-z+0x800);p.temporaryZ=w(p.baseZ+x);break;case 0x8000:p.temporaryX=w(p.baseX-x+0x800);p.temporaryZ=w(p.baseZ-z+0x800);break;default:p.temporaryX=w(p.baseX+z);p.temporaryZ=w(p.baseZ-x+0x800);break;}return {p.temporaryX,p.temporaryZ};
}
void pair(OpponentDrivingState& s,unsigned source,unsigned destination,std::uint16_t fraction){s.blendFactor=fraction;
    for(auto* values:{&s.coordinatesX,&s.coordinatesZ}){const auto difference=w((*values)[source+2]-(*values)[source]);(*values)[destination]=w(shr(sw(difference)*sw(fraction),8)+(*values)[source+((s.segmentHalf&0x80)?2:0)]);}
}
void bounds(OpponentDrivingState& s,unsigned index){s.interpolationScratch=interpolateTrackCoordinate({s.controls[0],s.controls[1],s.controls[2],s.controls[3],std::uint8_t(s.adjustedDistance),s.blend});const auto value=s.interpolationScratch;const auto shifted=(value>>3)|((value&0x80000000u)?0xe0000000u:0u);s.bounds[index]=std::uint16_t(shifted);s.scratch1=std::uint8_t(value>>24);s.scratch2=std::uint8_t(value>>16);s.progress.scratch3=std::uint8_t(value>>8);s.progress.scratch4=std::uint8_t(value);}
}
void updateOpponentLogic(const OpponentDrivingData& d,const OpponentDrivingInputs& in,OpponentDrivingState& s){
    if(!in.raceStartComplete||in.crashRecovery)return;validate(d,in,s);s.progress.segment=loadTrackSegmentConfiguration(d.track.track,s.segment);wheels(d,in,s);ai(d,in,s);target(in,s);avoidance(d,s);acceleration(s);
    s.progress.scratch3=s.progress.segment.alternateOrientation;
    const auto scaled=w(shr(sw(s.speed)*int(s.progress.scratch3)*128,15));const auto delta=timestep(scaled);const auto fractional=unsigned(s.speedFraction)+((delta&31u)<<3);s.speedFraction=b(int(fractional));s.subSegment=w(s.subSegment+shr(sw(delta),5)+(fractional>255?1:0));
    if(std::uint8_t(s.subSegment>>8)>=s.progress.segment.mode){s.subSegment=std::uint16_t((unsigned(b((s.subSegment>>8)-s.progress.segment.mode))<<8)|(s.subSegment&255));s.segment=b((s.segment+1)%d.track.track.segmentCount);}
}
void setupOpponentTrackGeometry(const OpponentDrivingData& d,const OpponentDrivingInputs& in,OpponentDrivingState& s){
    validate(d,in,s);s.trackDistance=behind(d,s,0x40);s.trackDirection=s.progress.currentSegment;config(d,s);segmentBase(d,s);s.heightDifference=w(s.progress.cameraRotation-((unsigned(s.progress.segment.slopeFlags)<<8)|in.slopeLowByte));s.progress.trackRotation=s.heightDifference;
    auto index=b((s.trackDistance>>8)*2);for(unsigned i=0;i<4;++i)s.controls[i]=control(d,s,b(index+i));index=b(index+4);
    if(index>=s.progress.segment.limit){next(d,s);config(d,s);s.controls[4]=control(d,s,2);s.controls[5]=control(d,s,3);previous(d,s);config(d,s);}else{s.controls[4]=control(d,s,index);s.controls[5]=control(d,s,b(index+1));}
    const auto base=offset(s.progress.segment.descriptorOffset);s.trackOffsetBase=b(read(d,base)+7);auto cursor=(s.progress.segment.reverseDirection)?b(s.trackOffsetBase+(s.progress.segment.mode-(s.trackDistance>>8)-1)*8):b(s.trackOffsetBase+(s.trackDistance>>8)*8);
    for(unsigned i=0;i<4;++i){const auto point=coordinate(d,s,cursor);cursor=b(cursor+4);const unsigned targetIndex=s.progress.segment.reverseDirection?3-i:i;s.coordinatesX[targetIndex]=point[0];s.coordinatesZ[targetIndex]=point[1];}
    for(unsigned side=0;side<2;++side){
        // Original D1 is0 then2: the second indexed read is the action timer
        // at1bbc0, rather than the adjacent offset byte at1bbbf.
        const auto displacement=side?s.actionTimer:s.segmentOffsets[0];
        const unsigned sum=unsigned(std::uint8_t(s.trackDistance))+displacement;const auto fraction=b(int(sum));s.segmentHalf=std::uint8_t((s.segmentHalf&0x7f)|(sum>255?0x80:0));pair(s,side,4+side*2,fraction);
    }
    // Each coordinate interpolator leaves D3 equal to its fraction. The
    // second ROXR therefore retains the first fraction's seven high bits.
    s.segmentHalf=std::uint8_t(b(int(std::uint8_t(s.trackDistance))+s.segmentOffsets[0])>>1);
    auto difference=w(s.wheelPosition[0]-s.wheelPosition[1]);if(difference&0x8000)difference=w(-int(difference));const auto factorIndex=shr(sw(difference),4)+2048;s.distanceFactor=d.distanceFactors[unsigned(factorIndex)];s.blend=std::uint8_t(s.trackDistance);
    const auto distanceWord=s.distanceOffset;
    s.adjustedDistance=w(distanceWord-s.distanceFactor);bounds(s,0);pair(s,4,5,s.adjustedDistance);
    s.adjustedDistance=w(distanceWord+s.distanceFactor);bounds(s,1);pair(s,4,7,s.adjustedDistance);
    const auto sharp=[&](){auto v=w(s.controls[0]-s.controls[2]);if(v&0x8000)v=w(-int(v));if(sw(v)>=20)s.curveSmoothing=0x80;};sharp();
    s.attenuated=w(s.coordinatesX[7]-s.coordinatesX[5]);s.attenuated=w(s.attenuated+half(s.attenuated));s.perpendicular=w(s.coordinatesZ[7]-s.coordinatesZ[5]);s.perpendicular=w(s.perpendicular+half(s.perpendicular));
    s.coordinatesX[8]=w(s.coordinatesX[5]-s.perpendicular);s.coordinatesZ[8]=w(s.coordinatesZ[5]+s.attenuated);s.coordinatesX[10]=w(s.coordinatesX[7]-s.perpendicular);s.coordinatesZ[10]=w(s.coordinatesZ[7]+s.attenuated);
    const auto shifted=unsigned(std::uint8_t(s.trackDistance))+128;if(shifted>255){s.controls[0]=s.controls[2];s.controls[1]=s.controls[3];s.controls[2]=s.controls[4];s.controls[3]=s.controls[5];}s.blend=b(int(shifted));s.adjustedDistance=std::uint16_t((s.adjustedDistance&0xff00)|s.lateral);bounds(s,2);sharp();(void)behind(d,s,255);
}
void initializeOpponentCoordinates(const OpponentDrivingData& d,const OpponentDrivingInputs& in,OpponentDrivingState& s){setupOpponentTrackGeometry(d,in,s);const auto jitter=b((random(s)&127)+0x68);for(unsigned i=0;i<4;++i)s.wheelPosition[i]=w(s.bounds[i]+jitter);}
} // namespace scr

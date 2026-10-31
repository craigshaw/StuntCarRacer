#include "render/near_track_geometry.h"
#include "physics/motion_transforms.h"
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
int sw(std::uint16_t x){return x<0x8000?int(x):int(x)-65536;}
int sb(std::uint8_t x){return x<0x80?int(x):int(x)-256;}
std::uint16_t w(int x){return static_cast<std::uint16_t>(x);}
std::uint8_t b(int x){return static_cast<std::uint8_t>(x);}
int shr(int x,unsigned n){const int d=1<<n;return x/d-(x%d<0);}
unsigned offset(std::uint16_t e){return std::uint16_t(((e<<8)|(e>>8))-0xb100);}
std::uint8_t read(const TrackProgressData& d,unsigned a){if(a>=d.track.geometry.size())throw std::out_of_range("Near geometry database read outside image");return d.track.geometry[a];}
std::uint16_t little(const TrackProgressData& d,unsigned a){return std::uint16_t(read(d,a)|(unsigned(read(d,a+1))<<8));}
NearTrackVertex& point(NearTrackGeometryState& s,unsigned index){
    if(index&1||index>=240)throw std::out_of_range("Near vertex byte index outside arrays");
    return index<120?s.upper[index/2]:s.lower[(index-120)/2];
}
std::uint8_t& flag(NearTrackGeometryState& s,unsigned index){if(index&3||index>=120)throw std::out_of_range("Near flag cross-section outside array");return s.flags[index/4];}
struct Polar{std::uint16_t angle{},distance{};};
Polar polar(const TrackProgressData& d,std::uint16_t x,std::uint16_t z){
    const auto ax=w(x&0x8000?-int(x):x),az=w(z&0x8000?-int(z):z);
    if(ax==0x8000||az==0x8000)throw std::out_of_range("Near geometry exceeds original signed magnitude domain");
    std::uint16_t ratio{},angle{};
    if(ax==az){ratio=0xffff;angle=0x2000;}
    else if(az<ax){
        ratio=std::uint16_t((std::uint32_t(az)<<16)/ax);angle=d.angleTable[ratio>>5];
        if(!((x^z)&0x8000))angle=w(-int(angle));
        angle=w(angle+((x&0x8000)?0xc000:0x4000));
        return {angle,w(int((std::uint32_t(d.magnitudeTable[ratio>>5])*az)>>16)+ax)};
    }else{ratio=std::uint16_t((std::uint32_t(ax)<<16)/az);angle=d.angleTable[ratio>>5];}
    if((x^z)&0x8000)angle=w(-int(angle));if(z&0x8000)angle=w(angle+0x8000);
    return {angle,w(int((std::uint32_t(d.magnitudeTable[ratio>>5])*ax)>>16)+az)};
}
void rotate(NearTrackVertex& p,const RenderProjectionCamera& c){
    const auto x=w(int(multiplyMatrixWord(w(p.x),c.cosRoll))+multiplyMatrixWord(w(p.y),c.sinRoll));
    const auto y=w(int(multiplyMatrixWord(w(p.y),c.cosRoll))-multiplyMatrixWord(w(p.x),c.sinRoll));
    p.x=shr(sw(x),2)+128;p.y=shr(sw(y),2)+64;
}
std::array<std::uint16_t,2> coordinates(const TrackProgressData& d,const NearTrackGeometryState& s,std::uint8_t index){
    const auto base=offset(s.progress.segment.descriptorOffset);auto x=little(d,base+index),z=little(d,base+b(index+2));
    switch(s.heightDifference&0xc0){
    case 0:return {w(s.progress.baseX+x),w(s.progress.baseZ+z)};
    case 0x40:return {w(s.progress.baseX-z+0x800),w(s.progress.baseZ+x)};
    case 0x80:return {w(s.progress.baseX-x+0x800),w(s.progress.baseZ-z+0x800)};
    default:return {w(s.progress.baseX+z),w(s.progress.baseZ-x+0x800)};
    }
}
std::uint8_t coordinateIndex(const NearTrackGeometryState& s,unsigned vertex){
    return s.progress.segment.reverseDirection?b(s.trackOffsetBase-int(vertex)*2):b(s.trackOffsetBase+int(vertex)*2);
}
void horizontal(const TrackProgressData& d,NearTrackGeometryState& s,NearTrackVertex& p,std::uint16_t x,std::uint16_t z){
    s.progress.temporaryX=x;s.progress.temporaryZ=z;
    p.inverseDepth=renderCoordinateInverseDepth(x,z,s.progress.cameraYaw,16);
    if(s.preview&0x80){
        x=w(shr(sw(x),1));z=w(shr(sw(z),1));s.perspectiveDepth=std::uint16_t((z&255)|((b((z>>8)+0x31))<<8));
        z=w(shr(sw(z),1)+0x4900);p.x=shr(sw(w(int(polar(d,x,z).angle)-s.progress.cameraYaw)),3);
    }else{const auto value=polar(d,x,z);s.perspectiveDepth=value.distance;p.x=shr(sw(w(int(value.angle)-s.progress.cameraYaw)),3);}
}
void vertical(const TrackProgressData& d,NearTrackGeometryState& s,NearTrackVertex& p){
    auto down=w(shr(sw(w(int(s.progress.cameraHeight)-p.height)),2));
    if(s.preview&0x80)down=multiplyMatrixWord(down,0x4c1b);
    p.y=shr(sw(w(int(polar(d,down,s.perspectiveDepth).angle)-s.camera.viewportCenterY)),3);
}
void segmentBase(const TrackProgressData& d,NearTrackGeometryState& s){
    const auto coordinate=d.segmentCoordinates.at(s.progress.currentSegment);
    auto x=b((coordinate&15)-s.progress.viewX),z=b((coordinate>>4)-s.progress.viewZ);
    switch(s.progress.cameraRotation){case 0x4000:{const auto old=x;x=b(-z);z=old;break;}case 0x8000:x=b(-x);z=b(-z);break;case 0xc000:{const auto old=x;x=z;z=b(-old);break;}default:break;}
    s.progress.transformedGridX=x;s.progress.transformedGridZ=z;
    s.progress.baseX=std::uint16_t((unsigned(b(x*8+s.progress.offsetXHigh))<<8)|(s.progress.baseX&255));
    s.progress.baseZ=std::uint16_t((unsigned(b(z*8+s.progress.offsetZHigh))<<8)|(s.progress.baseZ&255));
}
std::uint16_t height(const TrackProgressData& d,const NearTrackGeometryState& s,bool alternate,unsigned section){
    const auto& c=s.progress.segment;const unsigned base=offset(alternate?c.alternateOffset:c.primaryOffset);
    const auto raw=read(d,base+((c.format&0x80)?section*2:section));
    const auto value=(c.format&0x80)?std::uint16_t((unsigned(raw&0x7f)<<8)|read(d,base+section*2+1)):
        std::uint16_t(((raw&15)<<8)|((raw<<1)&0xe0));
    return w(value+(alternate?c.alternateBase:c.primaryBase));
}
void setup(const TrackProgressData& d,NearTrackGeometryState& s){
    const auto& c=s.progress.segment;
    const auto budgetWord=w(46-int(s.progress.visibility));const auto budget=std::uint8_t((budgetWord&0x8000)?0:budgetWord);
    s.progress.visibility=w(s.progress.visibility+c.mode);
    flag(s,c.maximumDoubled)=s.progress.currentSegment==s.lapApproachSegment?1:0;
    s.processingIndex=s.startIndex?1:0;
    for(unsigned index=s.startIndex;index<=c.maximumDoubled;index+=4){
        const unsigned section=index/4;const auto primary=read(d,offset(c.primaryOffset)+((c.format&0x80)?section*2:section));
        const bool endpoint=index==0||index==c.maximumDoubled;
        if(!(primary&0x80)&&!endpoint&&section>=budget){point(s,index).height=point(s,index+2).height=0x8000;}
        else{
            if(primary&0x80)flag(s,index)=0;
            const auto raw=height(d,s,false,section);point(s,index).height=raw;
            if(!(flag(s,index)&0x80)){
                auto previous=index>=4?point(s,index-4).height:std::uint16_t(0x8000);
                bool compare=index>=4;
                if(previous&0x8000){if(s.continuation&&index>=4)previous=height(d,s,false,section-1);else compare=false;}
                auto difference=w(int(raw)-previous);if(difference&0x8000)difference=w(-int(difference));
                if(compare&&sw(difference)>=0x280)flag(s,index)|=0x20;
            }
            point(s,index+2).height=height(d,s,true,section);
        }
        if(c.format&0x80)s.processingIndex=b(int(section)+1);
    }
    const unsigned last=c.maximumDoubled,section=last/4;
    const auto lastRaw=read(d,offset(c.primaryOffset)+((c.format&0x80)?section*2:section));
    if(lastRaw&0x80){flag(s,last)=0x80;if(c.mode>=budget)point(s,last).height=point(s,last+2).height=0x8000;}
}
void admitLower(const TrackProgressData& d,NearTrackGeometryState& s,unsigned index){
    const unsigned side=(index&2)/2;auto previous=s.processedIndices[side];bool visible=false;
    if(previous&0x80)visible=(s.edgeVisibilityMode&0x80)||(s.progress.offRoadSide&&((s.progress.offRoadSide^index)&2)==0);
    else{
        const auto oldX=previous<4&&!(s.edgeVisibilityMode&0x80)?s.transformedCoordinates[previous/2]:w(point(s,previous).x);
        const auto difference=w(side?point(s,index).x-int(oldX):int(oldX)-point(s,index).x);
        visible=!(difference&0x8000);
    }
    if(visible){auto& lower=point(s,index+120);lower.height=512;lower.x=point(s,index).x;lower.inverseDepth=point(s,index).inverseDepth;vertical(d,s,lower);rotate(lower,s.camera);}
}
} // namespace
void projectNearTrackSegment(const TrackProgressData& d,NearTrackGeometryState& s){
    s.progress.segment=loadTrackSegmentConfiguration(d.track,s.progress.currentSegment);segmentBase(d,s);
    s.heightDifference=b((s.progress.cameraRotation>>8)-s.progress.segment.slopeFlags);
    s.progress.trackRotation=std::uint16_t((unsigned(s.heightDifference)<<8)|(s.progress.trackRotation&255));
    const auto& c=s.progress.segment;if(c.limitDoubled>120||c.limitDoubled<4||s.startIndex&3)throw std::out_of_range("Near segment exceeds original coordinate buffer");
    setup(d,s);
    if(s.renderingLoopIndex){if(s.renderingLoopIndex<4)throw std::out_of_range("Invalid near rendering loop index");s.savedHeights={point(s,s.renderingLoopIndex-4).height,point(s,s.renderingLoopIndex-2).height};}
    // The original MOVE.B #0 at0x652b8 makes its BPL unconditional. The
    // alternate array-reversal block below that branch is unreachable.
    if(!(s.displayUpdate&0x80))s.reverseDecision=0;
    s.trackOffsetBase=b(read(d,offset(c.descriptorOffset))+7);
    if(c.reverseDirection)s.trackOffsetBase=b(s.trackOffsetBase+(c.limit-1)*4);
    for(unsigned index=s.startIndex;index<c.limitDoubled;index+=2){
        auto& upper=point(s,index);if(upper.height&0x8000)continue;
        point(s,index+120).height=0x8000;
        if(sb(b(index))<sb(s.renderingLoopIndex)){upper.height=0x8000;continue;}
        const auto xy=coordinates(d,s,coordinateIndex(s,index));horizontal(d,s,upper,xy[0],xy[1]);vertical(d,s,upper);
        const auto magnitude=w(upper.x<0?-upper.x:upper.x);
        if((s.progress.offTrackFlags&0x40)&&sw(magnitude)>=0xc00)upper.height=0x8000;
        else admitLower(d,s,index);
        s.processedIndices[(index&2)/2]=b(index);
    }
    for(unsigned side=0;side<2;++side){const auto index=s.processedIndices[side];if(index&0x80)throw std::invalid_argument("Near segment has no processed side vertex");s.transformedCoordinates[side]=w(point(s,index).x);}
    for(unsigned index=s.startIndex;index<c.limitDoubled;index+=2)if(!(point(s,index).height&0x8000))rotate(point(s,index),s.camera);
    s.lastProcessed=s.processedIndices[0];
}
void materializeNearTrackLower(const TrackProgressData& d,NearTrackGeometryState& s,std::uint8_t index){
    if(index>=120||index&1)throw std::out_of_range("Invalid near lower vertex index");
    s.trackOffsetBase=b(read(d,offset(s.progress.segment.descriptorOffset))+7);
    const auto adjusted=s.progress.segment.reverseDirection?b(s.trackOffsetBase+(s.progress.segment.limit-1)*4-int(index)*2):b(s.trackOffsetBase+int(index)*2);
    const auto xy=coordinates(d,s,adjusted);auto& lower=point(s,index+120);lower.height=512;
    horizontal(d,s,lower,xy[0],xy[1]);vertical(d,s,lower);rotate(lower,s.camera);
}
bool shiftNearTrackCoordinates(NearTrackGeometryState& s){
    auto index=(s.lastProcessed&0x80)?s.progress.segment.limitDoubled:b(s.lastProcessed+4);
    if(!(s.viewReverse&0x80)){
        if(index<4)throw std::out_of_range("Near coordinate carry underflows");
        for(int target=2;target>=0;target-=2){index=b(index-2);point(s,unsigned(target))=point(s,index);point(s,unsigned(target)+120)=point(s,index+120);}
        s.processedIndices={0,2};return nearTrackInsideX(point(s,index).x,s.horizontalPadding);
    }
    s.startIndex=0;index=b(index-4);return nearTrackInsideX(point(s,index).x,s.horizontalPadding);
}
std::array<NearTrackCutEdge,4> prepareNearTrackCutEdges(const TrackProgressData& d,NearTrackGeometryState& s){
    std::array<NearTrackCutEdge,4> result{};if(!s.renderingLoopIndex)return result;
    for(unsigned n=0;n<4;++n){
        const unsigned from=s.renderingLoopIndex+(n&1)*2+(n>=2?120:0);auto& current=point(s,from);
        auto& previous=point(s,from-4);auto& edge=result[n];edge.fromIndex=b(from);edge.toIndex=b(from-4);
        if((n>=2&&(current.height&0x8000))||!nearTrackInsideX(current.x,s.horizontalPadding)||!nearTrackInsideY(current.y,s.topPadding,s.bottomPadding))continue;
        edge.valid=true;previous.height=n>=2?512:s.savedHeights[n&1];
        const unsigned section=s.progress.progressOffset>>8;
        const auto base=b(read(d,offset(s.progress.segment.descriptorOffset))+7);
        s.trackOffsetBase=base;
        auto coordinateAt=[&](unsigned row){
            const auto descriptor=s.progress.segment.reverseDirection?b(base+(s.progress.segment.mode-int(section)-1)*8+(12-int(row)*4)):
                b(base+section*8+row*4);
            return coordinates(d,s,descriptor);
        };
        const auto first=coordinateAt(n&1),second=coordinateAt((n&1)+2);
        const auto fraction=std::uint8_t(s.progress.progressOffset);
        const auto interpolate=[&](unsigned axis){const auto difference=w(int(second[axis])-first[axis]);return w(shr(sw(difference)*int(fraction),8)+((s.segmentHalfFlags&0x80)?second[axis]:first[axis]));};
        if(n<2){const auto difference=w(int(current.height)-previous.height);previous.height=w(previous.height+multiplyMatrixWord(difference,std::uint16_t((unsigned(fraction)<<7)&0x7fff)));}
        horizontal(d,s,previous,interpolate(0),interpolate(1));vertical(d,s,previous);if(!(previous.height&0x8000))rotate(previous,s.camera);
        if(nearTrackInsideX(previous.x,s.horizontalPadding)&&nearTrackInsideY(previous.y,s.topPadding,s.bottomPadding)){
            const auto dx=w(previous.x-current.x);auto dy=w(previous.y-current.y);if(!dy&&!dx)dy=1;
            const auto depthStep=previous.inverseDepth-current.inverseDepth;
            unsigned guard=0;do{previous.x=sw(w(previous.x+dx));previous.y=sw(w(previous.y+dy));previous.inverseDepth=std::max(1e-9,previous.inverseDepth+depthStep);if(++guard>65536)throw std::runtime_error("Near boundary ray did not exit viewport");}while(nearTrackInsideX(previous.x,s.horizontalPadding)&&nearTrackInsideY(previous.y,s.topPadding,s.bottomPadding));
        }
    }
    return result;
}
} // namespace scr

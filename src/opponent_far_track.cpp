#include "opponent_far_track.h"
#include "motion_transforms.h"
#include "track_sides.h"
#include <algorithm>
#include <stdexcept>

namespace scr {namespace {
int sw(unsigned x){x&=65535;return x<32768?int(x):int(x)-65536;}
int sb(unsigned x){x&=255;return x<128?int(x):int(x)-256;}
std::uint16_t w(int x){return std::uint16_t(x);}
std::uint8_t b(int x){return std::uint8_t(x);}
int shr(int x,unsigned n){const int d=1<<n;return x/d-(x%d<0);}
struct Polar{std::uint16_t angle{},distance{};};
Polar polar(const RenderProjectionTables& t,std::uint16_t x,std::uint16_t z){
    const auto ax=w(x&0x8000?-int(x):x),az=w(z&0x8000?-int(z):z);if(ax==0x8000||az==0x8000)throw std::out_of_range("Far track outside original angular magnitude domain");
    std::uint16_t ratio{},angle{};if(ax==az){ratio=0xffff;angle=0x2000;}
    else if(az<ax){ratio=std::uint16_t((std::uint32_t(az)<<16)/ax);angle=t.angleTable[ratio>>5];if(!((x^z)&0x8000))angle=w(-int(angle));angle=w(angle+((x&0x8000)?0xc000:0x4000));return {angle,w(int((std::uint32_t(t.magnitudeTable[ratio>>5])*az)>>16)+ax)};}
    else{ratio=std::uint16_t((std::uint32_t(ax)<<16)/az);angle=t.angleTable[ratio>>5];}if((x^z)&0x8000)angle=w(-int(angle));if(z&0x8000)angle=w(angle+0x8000);return {angle,w(int((std::uint32_t(t.magnitudeTable[ratio>>5])*ax)>>16)+az)};
}
void rotate(NearTrackVertex& p,const RenderProjectionCamera& c){const auto x=w(int(multiplyMatrixWord(w(p.x),c.cosRoll))+multiplyMatrixWord(w(p.y),c.sinRoll));const auto y=w(int(multiplyMatrixWord(w(p.y),c.cosRoll))-multiplyMatrixWord(w(p.x),c.sinRoll));p.x=shr(sw(x),2)+128;p.y=shr(sw(y),2)+64;}
int ratioStep(int small,int large,int distance){if(large>=small)return large==small?distance:int(((std::uint64_t(unsigned(small))<<16)/unsigned(large))*unsigned(distance)>>16);return distance>=large?small:int(((std::uint64_t(unsigned(distance))<<16)/unsigned(large))*unsigned(small)>>16);}
unsigned horizonStorage(NearTrackPoint a,NearTrackPoint c,int padding,int topPadding){
    const auto ordinary=makeNearTrackEdge(a,c,padding,topPadding);if(ordinary.clipFlags&15)return ordinary.storageBytes;if(a.y<c.y)std::swap(a,c);
    const auto clipY=[](NearTrackPoint& p,const NearTrackPoint& q,int y){const int dx=sw(unsigned(q.x-p.x)),dy=sw(unsigned(q.y-p.y));const int step=ratioStep(std::abs(dx),std::abs(dy),std::abs(y-p.y));p.x=sw(unsigned(p.x+(dx<0?-step:step)));p.y=y;};
    if(a.y<-topPadding)clipY(a,c,-topPadding);else if(a.y>=128)clipY(a,c,128);if(c.y<-topPadding)clipY(c,a,-topPadding);else if(c.y>=128)clipY(c,a,128);
    const auto clipX=[](NearTrackPoint& p,const NearTrackPoint& q,int x){const int dx=sw(unsigned(q.x-p.x)),dy=sw(unsigned(q.y-p.y));const int step=ratioStep(std::abs(dy),std::abs(dx),std::abs(x-p.x));p.y=sw(unsigned(p.y+(dy<0?-step:step)));p.x=x;};
    if(a.x<-padding)clipX(a,c,-padding);else if(a.x>=256+padding)clipX(a,c,256+padding);if(c.x<-padding)clipX(c,a,-padding);else if(c.x>=256+padding)clipX(c,a,256+padding);
    return 10+2*unsigned(std::abs(a.y-c.y));
}
NearTrackPoint point(const NearTrackVertex& p){return {sw(unsigned(p.x)),sw(unsigned(p.y))};}
NearTrackEdge line(OpponentFarTrackFrame& out,const NearTrackVertex& a,const NearTrackVertex& c,unsigned offset,unsigned index){
    NearTrackEdge e{};e.clipFlags=0x80;e.originQueueOffset=std::uint16_t(offset);e.originEdge=std::uint8_t(index);
    if((a.height|c.height)&0x8000||out.queue.lineBytes>=0x2328)return e;e=makeNearTrackEdge(point(a),point(c),out.queue.horizontalPadding,out.queue.topPadding);e.originQueueOffset=std::uint16_t(offset);e.originEdge=std::uint8_t(index);out.queue.lineBytes=std::uint16_t(out.queue.lineBytes+e.storageBytes);return e;
}
void quad(std::vector<NearTrackDrawCommand>& out,const NearTrackQueueState& q,unsigned end,unsigned first,NearTrackSurface surface,unsigned color,std::array<const NearTrackEdge*,4> edges){
    unsigned common=15;for(const auto* e:edges){if(!e->present)return;common&=e->clipFlags;}if(common)return;
    NearTrackDrawCommand c{};c.kind=NearTrackCommandKind::fill;c.surface=surface;c.color=std::uint8_t(color);c.originQueueOffset=std::uint16_t(end);c.firstQueueOffset=std::uint16_t(first);for(auto e:edges)c.edges.push_back(*e);out.push_back(std::move(c));(void)q;
}
} // namespace
OpponentFarTrackFrame buildOpponentFarTrack(const OpponentFarTrackData& data,const RenderProjectionTables& tables,const NearTrackGeometryState& initial,const NearTrackQueueState& dense){
    if(data.mesh.sections.empty()||!data.mesh.segmentCount)throw std::invalid_argument("Far track needs generated cache sections");OpponentFarTrackFrame out{};out.geometry=initial;out.queue=dense;out.denseEnd=dense.offset;auto& s=out.geometry;
    auto cursor=std::lower_bound(data.mesh.sections.begin(),data.mesh.sections.end(),s.progress.currentSegment,[](const TrackMeshSection& a,unsigned segment){return a.segment<segment;})-data.mesh.sections.begin();if(cursor==std::ptrdiff_t(data.mesh.sections.size()))cursor=0;
    unsigned index=4;auto previous=initial.transformedCoordinates;
    do{
        const auto& section=data.mesh.sections[std::size_t(cursor)];s.progress.currentSegment=section.segment;out.headers[index/4]={section.flags,section.segment};
        const auto cell=data.mesh.segmentCoordinates.at(section.segment);auto gx=b((cell&15)-s.progress.viewX),gz=b((cell>>4)-s.progress.viewZ);
        switch(s.progress.cameraRotation){case 0x4000:{const auto old=gx;gx=b(-gz);gz=old;break;}case 0x8000:gx=b(-gx);gz=b(-gz);break;case 0xc000:{const auto old=gx;gx=gz;gz=b(-old);break;}default:break;}
        const auto ox=w(sb(gx)*1024+shr(sw((unsigned(s.progress.offsetXHigh)<<8)|(s.progress.baseX&255)),1));const auto oz=w(sb(gz)*1024+shr(sw((unsigned(s.progress.offsetZHigh)<<8)|(s.progress.baseZ&255)),1));
        for(unsigned side=0;side<2;++side){const auto& raw=section.points[side];auto x=raw[0],z=raw[1];switch(s.progress.cameraRotation){case 0x4000:{const auto old=x;x=w(0x800-int(z));z=old;break;}case 0x8000:x=w(0x800-int(x));z=w(0x800-int(z));break;case 0xc000:{const auto old=x;x=z;z=w(0x800-int(old));break;}default:break;}
            const auto horizontal=polar(tables,w(shr(sw(x),1)+ox),w(shr(sw(z),1)+oz));auto& p=s.upper[index/2+side];p.height=raw[2];p.x=shr(sw(w(int(horizontal.angle)-s.progress.cameraYaw)),3);
            const auto down=w(shr(sw(w(int(s.progress.cameraHeight)-p.height)),3));p.y=shr(sw(w(int(polar(tables,down,horizontal.distance).angle)-s.camera.viewportCenterY)),3);
            const auto old=previous[side];previous[side]=w(p.x);auto& lower=s.lower[index/2+side];
            if(trackSideVertexRequired(side?TrackSide::left:TrackSide::right,w(p.x),old,s.progress.offRoadSide,section.flags)){
                lower.height=512;lower.x=p.x;const auto ground=w(shr(sw(w(int(s.progress.cameraHeight)-512)),3));lower.y=shr(sw(w(int(polar(tables,ground,horizontal.distance).angle)-s.camera.viewportCenterY)),3);rotate(lower,s.camera);
            }else lower.height=0x8000;rotate(p,s.camera);
        }
        cursor=(cursor+1)%std::ptrdiff_t(data.mesh.sections.size());index+=4;
        if(sb(index)>=sb(data.projectionLimits.at(section.segment)))break;
        const auto a=index/2-2,c=index/2-1;if(nearTrackInsideX(s.upper[a].x,s.horizontalPadding)||nearTrackInsideX(s.upper[c].x,s.horizontalPadding))continue;
        if(!(s.lower[a].height&0x8000)){if(nearTrackInsideX(s.lower[a].x,s.horizontalPadding))continue;break;}
        if(!(s.lower[c].height&0x8000)&&nearTrackInsideX(s.lower[c].x,s.horizontalPadding))continue;break;
    }while(index<120);
    out.vertexEnd=b(index);bool opponentFound=false;
    for(unsigned at=4;at<index&&out.queue.offset<0x5e0;at+=4){const unsigned end=out.queue.offset,current=at/2,prior=current-2;auto& e=out.queue.entries.at(end/32);
        e.edges[0]=line(out,s.upper[current],s.upper[prior],end,0);e.edges[1]=line(out,s.upper[current+1],s.upper[prior+1],end,1);
        e.edges[2]=line(out,s.lower[current],s.lower[prior],end,2);e.edges[3]=line(out,s.lower[current+1],s.lower[prior+1],end,3);
        e.edges[4]=line(out,s.upper[current],s.lower[current],end,4);e.edges[5]=line(out,s.lower[current+1],s.upper[current+1],end,5);e.edges[6]=line(out,s.upper[current+1],s.upper[current],end,6);
        e.segment=out.headers[at/4][1];e.processedFlags=out.headers[at/4][0];
        if(!opponentFound&&e.segment==out.queue.opponentSegment){unsigned marker=end;if(sw(out.queue.minimumBoundaryDistance)>=-256&&!(e.processedFlags&0x20))marker-=32;out.queue.opponentOffset=std::uint16_t(marker&~31u);opponentFound=true;}
        out.queue.offset=std::uint16_t(end+32);
    }return out;
}
std::vector<NearTrackDrawCommand> drawOpponentFarTrack(const OpponentFarTrackFrame& frame,const NearTrackDrawInputs& in,const NearTrackPainter& painter){
    std::vector<NearTrackDrawCommand> out;const auto& q=frame.queue;if(q.offset<32)return out;
    for(unsigned end=q.offset-32;end>=frame.denseEnd&&end>=32;end-=32){const unsigned first=end-32;const auto& c=q.entries.at(end/32);const auto& p=q.entries.at(first/32);
        const auto side=[&](unsigned which){const auto color=trackSideFillColor(c.segment,{in.sideEdgeColor,in.alternateSideColor,false});if(which)quad(out,q,end,first,NearTrackSurface::leftSide,color,{&c.edges[3],&p.edges[5],&c.edges[1],&c.edges[5]});else quad(out,q,end,first,NearTrackSurface::rightSide,color,{&c.edges[2],&c.edges[4],&c.edges[0],&p.edges[4]});};
        const auto road=[&](){quad(out,q,end,first,NearTrackSurface::road,trackRoadFillColor(c.segment,c.processedFlags),{&c.edges[6],&c.edges[1],&c.edges[0],&p.edges[6]});if(c.processedFlags&1){NearTrackDrawCommand l{};l.kind=NearTrackCommandKind::line;l.surface=NearTrackSurface::road;l.color=15;l.originQueueOffset=std::uint16_t(end);l.firstQueueOffset=std::uint16_t(first);if(c.edges[6].present){l.edges.push_back(c.edges[6]);out.push_back(std::move(l));}}};
        const auto car=[&](NearTrackPainterSlot slot){if(painter)painter(slot,std::uint16_t(first),out);};
        if(!in.renderingOrderMode){side(0);side(1);car(NearTrackPainterSlot::ahead);car(NearTrackPainterSlot::behind);road();car(NearTrackPainterSlot::occluded);}
        else if(sb(unsigned(in.offsetFromRoadCenter)>>8)<0){car(NearTrackPainterSlot::ahead);side(1);road();car(NearTrackPainterSlot::occluded);side(0);car(NearTrackPainterSlot::behind);}
        else{car(NearTrackPainterSlot::behind);side(0);road();car(NearTrackPainterSlot::occluded);side(1);car(NearTrackPainterSlot::ahead);}
    }return out;
}
std::uint16_t allocateOpponentBackgroundLines(std::uint16_t bytes,const MountainHorizonData& data,
    std::uint16_t yaw,const RenderProjectionCamera& camera,std::uint8_t mode,NearTrackGeometryState* history){
    const auto horizonPoint=[&](int side){const auto down=w(-shr(sw(camera.viewportCenterY),3)-((mode&0x80)?0:8));
        const auto x=w(int(multiplyMatrixWord(w(side),camera.cosRoll))+multiplyMatrixWord(down,camera.sinRoll));
        const auto y=w(int(multiplyMatrixWord(down,camera.cosRoll))-multiplyMatrixWord(w(side),camera.sinRoll));
        return NearTrackPoint{shr(sw(x),2)+128,shr(sw(y),2)+64};};
    const int padding=history?history->horizontalPadding:0,topPadding=history?history->topPadding:0;
    const auto first=horizonPoint(-0x500),last=horizonPoint(0x500);
    if(history){history->upper[2].x=first.x;history->upper[2].y=first.y;history->upper[3].x=last.x;history->upper[3].y=last.y;}
    bytes=std::uint16_t(bytes+horizonStorage(first,last,padding,topPadding));
    const auto mountains=projectMountainHorizon(data,yaw,camera,padding);
    for(const auto& instance:mountains.instances){
        if(history)for(unsigned n=0;n<instance.vertices.size();++n){auto& p=n<60?history->upper.at(n):history->lower.at(n-60);p.x=instance.vertices[n].x;p.y=instance.vertices[n].y;}
        for(const auto& edge:data.shapes.at(instance.shapeIndex).edges){
            if(bytes>=0x2328)continue;const auto& a=instance.vertices.at(edge[0]);const auto& c=instance.vertices.at(edge[1]);
            bytes=std::uint16_t(bytes+makeNearTrackEdge({a.x,a.y},{c.x,c.y},padding,topPadding).storageBytes);
        }
    }return bytes;
}
} // namespace scr

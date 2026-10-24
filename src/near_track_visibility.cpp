#include "near_track_visibility.h"
#include "track_sides.h"
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
int sw(unsigned x){x&=65535;return x<32768?int(x):int(x)-65536;}
int sb(unsigned x){x&=255;return x<128?int(x):int(x)-256;}
bool valid(const NearTrackVertex& v){return !(v.height&0x8000);}
NearTrackPoint point(const NearTrackVertex& v){return {sw(unsigned(v.x)),sw(unsigned(v.y))};}
NearTrackVertex& vertex(NearTrackGeometryState& s,unsigned index){
    if(index>=0xf0||(index&1))throw std::out_of_range("Near vertex byte index outside buffers");
    return index>=0x78?s.lower[(index-0x78)/2]:s.upper[index/2];
}
const NearTrackVertex& vertex(const NearTrackGeometryState& s,unsigned index){
    if(index>=0xf0||(index&1))throw std::out_of_range("Near vertex byte index outside buffers");
    return index>=0x78?s.lower[(index-0x78)/2]:s.upper[index/2];
}
unsigned outcode(NearTrackPoint p,int padding,int topPadding,int bottomPadding){return (p.x<-padding?8:p.x>=256+padding?4:0)|(p.y<-topPadding?2:p.y>=128+bottomPadding?1:0);}
int ratioStep(int dx,int dy,int distance){
    if(dx<0||dy<=0||distance<0)throw std::out_of_range("Near clipping ratio outside original magnitude domain");
    if(dy>=dx)return dy==dx?distance:int(((std::uint64_t(unsigned(dx))<<16)/unsigned(dy))*unsigned(distance)>>16);
    return distance>=dy?dx:int(((std::uint64_t(unsigned(distance))<<16)/unsigned(dy))*unsigned(dx)>>16);
}
void clipY(NearTrackPoint& a,const NearTrackPoint& b,int y){
    const int dx=sw(unsigned(b.x-a.x)),dy=sw(unsigned(b.y-a.y));
    if(!dy)throw std::out_of_range("Near clipping encountered horizontal outside edge");
    const int step=ratioStep(std::abs(dx),std::abs(dy),std::abs(y-a.y));
    a.x=sw(unsigned(a.x+(dx<0?-step:step)));a.y=y;
}
unsigned lineBytes(const NearTrackEdge& e){
    return e.present?e.storageBytes:0;
}
NearTrackEdge absent(){NearTrackEdge e{};e.clipFlags=0x80;return e;}
void storeEdge(NearTrackQueueState& q,unsigned offset,NearTrackEdge edge){
    if(offset>=q.entries.size()*32||offset%32>=28||(offset&3))throw std::out_of_range("Near queue edge offset outside slots");
    if(edge.present){if(q.lineBytes>=0x2328)edge=absent();else q.lineBytes=std::uint16_t(q.lineBytes+lineBytes(edge));}
    edge.originQueueOffset=std::uint16_t(offset&~31u);edge.originEdge=std::uint8_t((offset&31)/4);
    q.entries[offset/32].edges[(offset&31)/4]=edge;
}
void storeLine(NearTrackQueueState& q,unsigned offset,const NearTrackGeometryState& s,unsigned a,unsigned b){
    if(valid(vertex(s,a))&&valid(vertex(s,b)))storeEdge(q,offset,makeNearTrackEdge(point(vertex(s,a)),point(vertex(s,b)),q.horizontalPadding,q.topPadding,q.bottomPadding));
    else storeEdge(q,offset,absent());
}
void manageBounds(NearTrackQueueState& q){
    q.previousOffset=255;int offset=int(q.offset)-32;
    if(sw(q.minimumBoundaryDistance)>=-256&&!(q.entries.at(unsigned(offset)/32).processedFlags&0x20))offset-=32;
    q.opponentOffset=std::uint16_t(offset&~31);
}
void adjustBounds(NearTrackGeometryState& s,unsigned index){
    for(unsigned side=0;side<2;++side){auto& v=vertex(s,index+side*2);int x=sw(unsigned(v.x-128));if(x<0)x=sw(unsigned(-x));x=sw(unsigned(x-160-s.horizontalPadding));if(x>=0)v.y=sw(unsigned(v.y+(unsigned(x)>>3)));}
}
std::uint8_t previousOffset(const NearTrackGeometryState& s,const NearTrackQueueState& q){return s.progress.currentSegment==q.opponentSegment?std::uint8_t(q.opponentSubSegment<<2):255;}
NearTrackPoint endpoint(const NearTrackEdge& e,unsigned flags){return flags&0x40?e.clippedFrom:e.clippedTo;}
int perimeter(NearTrackPoint p,int padding,int topPadding,int bottomPadding){const int width=256+2*padding,height=128+topPadding+bottomPadding;p.x+=padding;p.y+=topPadding;if(!p.y)return p.x;if(p.x==width)return width+p.y;if(p.y==height)return 2*width+height-p.x;return 2*(width+height)-p.y;}
bool closeBoundary(std::vector<NearTrackEdge>& edges,NearTrackPoint start,NearTrackPoint target,
    unsigned renderingFlag,const NearTrackDrawInputs& in,std::uint16_t* lineBytes,int padding,int topPadding,int bottomPadding){
    const int left=-padding,right=256+padding,top=-topPadding,bottom=128+bottomPadding,middle=(bottom-topPadding)/2,halfPerimeter=384+2*padding+topPadding+bottomPadding;
    const int delta=perimeter(target,padding,topPadding,bottomPadding)-perimeter(start,padding,topPadding,bottomPadding);
    const bool required=delta<0?-delta<halfPerimeter:delta>=halfPerimeter;
    bool swap=false;
    if(required){
        if(!renderingFlag)swap=in.offRoadSideFlags!=0;
        else{
            swap=true;const int lateral=std::abs(sw(in.offsetFromRoadCenter));
            if(in.offRoadSideFlags&&(renderingFlag^in.offRoadSideFlags)==0&&lateral>=197&&lateral<576){
                if(renderingFlag&0x80){if((start.y==top||(start.x==right&&start.y<middle))&&target.y>start.y)swap=false;}
                else if((target.y==top||(target.x==left&&target.y<middle))&&start.y>target.y)swap=false;
            }
        }
    }
    if(swap)std::swap(start,target);
    int savedX=start.x,side=start.x==right?0:start.x==left?2:start.y==bottom?1:start.y==top?3:-1;
    if(side<0)return false;
    const auto append=[&](NearTrackPoint a,NearTrackPoint c){
        // Original clipLineAndMakeEdge0x6814a admits a line at the arena's
        // exact end; its guard is strict>, unlike drawClippedLine's>= guard.
        if(lineBytes&&*lineBytes>0x2328)return false;
        auto e=makeNearTrackEdge(a,c,padding,topPadding,bottomPadding);e.originQueueOffset=0xffff;e.originEdge=0xff;
        if(lineBytes)*lineBytes=std::uint16_t(*lineBytes+e.storageBytes);edges.push_back(e);return true;
    };
    for(unsigned n=0;n<12;++n){
        if(side==0){
            const int end=target.x==right&&start.y<=target.y?target.y:bottom;
            if(!append({right,start.y},{right,end}))return false;
            if(target.x==right&&start.y<=target.y)return true;start.y=bottom;
        }else if(side==1){if(target.y==bottom&&savedX>=target.x)return true;savedX=left;}
        else if(side==2){
            const int end=target.x==left&&start.y>=target.y?target.y:top;
            if(!append({left,start.y},{left,end}))return false;
            if(target.x==left&&start.y>=target.y)return true;start.y=top;
        }else{if(target.y==top&&savedX<=target.x)return true;savedX=right;}
        side=(side+1)%4;
    }
    return false;
}
struct FillResult {unsigned barrierOffset{};};
FillResult fill(std::vector<NearTrackDrawCommand>& output,const NearTrackQueueState& q,const NearTrackDrawInputs& in,
    unsigned end,unsigned first,NearTrackSurface surface,std::uint16_t* lineBytes){
    const unsigned vertical=surface==NearTrackSurface::road?6:surface==NearTrackSurface::rightSide?4:5;
    const unsigned frontIndex=surface==NearTrackSurface::road?1:surface==NearTrackSurface::rightSide?0:3;
    const unsigned backIndex=surface==NearTrackSurface::leftSide?1:surface==NearTrackSurface::rightSide?2:0;
    unsigned cursor=end,barrierOffset=end;const NearTrackEdge* initial=&q.entries.at(cursor/32).edges[vertical];
    if(surface!=NearTrackSurface::road){
        while(!initial->present){if(cursor<32||cursor-32<=first)return {barrierOffset};cursor-=32;initial=&q.entries.at(cursor/32).edges[vertical];}
        barrierOffset=cursor;
    }else if(!initial->present)return {barrierOffset};
    const auto& header=q.entries.at(cursor/32);
    NearTrackDrawCommand command{};command.kind=NearTrackCommandKind::fill;command.surface=surface;
    command.originQueueOffset=std::uint16_t(end);command.firstQueueOffset=std::uint16_t(first);
    command.color=surface==NearTrackSurface::road?trackRoadFillColor(header.segment,header.processedFlags):
        trackSideFillColor(header.segment,{in.sideEdgeColor,in.alternateSideColor,in.previewColorOverride},true);
    std::vector<NearTrackEdge> front{*initial},back;unsigned clip=initial->clipFlags;
    unsigned frontFlags=initial->clipFlags^0x40,backFlags=initial->clipFlags;
    bool special=cursor==32;
    while(!special){
        const auto& a=q.entries.at(cursor/32).edges[frontIndex];clip&=a.clipFlags;
        if(!a.present){if(cursor==end)return {barrierOffset};break;}
        frontFlags=a.clipFlags;front.push_back(a);
        const auto& b=q.entries.at(cursor/32).edges[backIndex];clip&=b.clipFlags;
        if(!b.present){front.pop_back();if(cursor==end)return {barrierOffset};break;}
        backFlags=b.clipFlags;back.push_back(b);cursor-=32;
        if(cursor==first)break;special=cursor==32;
    }
    if(special){
        const auto& a=q.entries.at(cursor/32).edges[frontIndex];clip&=a.clipFlags;if(a.present){frontFlags=a.clipFlags;front.push_back(a);}
        const auto start=endpoint(front.back(),frontFlags);
        const auto& b=q.entries.at(cursor/32).edges[backIndex];clip&=b.clipFlags;if(b.present){backFlags=b.clipFlags;back.push_back(b);}
        // A2 still equals the shared edge-sort base when no backward-chain
        // edge exists. Original extraction then reads the initial edge just
        // written at that base, rather than an absent edge sentinel.
        const auto target=endpoint(back.empty()?front.front():back.back(),backFlags);
        if(!closeBoundary(front,start,target,surface==NearTrackSurface::rightSide?0x80:surface==NearTrackSurface::leftSide?2:0,in,lineBytes,q.horizontalPadding,q.topPadding,q.bottomPadding))return {barrierOffset};
    }else{
        const auto& edge=q.entries.at(cursor/32).edges[vertical];clip&=edge.clipFlags;if(!edge.present)return {barrierOffset};front.push_back(edge);
    }
    if(clip&15)return {barrierOffset};
    command.edges=std::move(front);command.edges.insert(command.edges.end(),back.begin(),back.end());output.push_back(std::move(command));
    return {barrierOffset};
}
void lineCommand(std::vector<NearTrackDrawCommand>& out,const NearTrackEdge& e,unsigned color,NearTrackSurface surface,unsigned end,unsigned first){
    if(!e.present)return;NearTrackDrawCommand c{};c.kind=NearTrackCommandKind::line;c.surface=surface;c.color=std::uint8_t(color);
    c.originQueueOffset=std::uint16_t(end);c.firstQueueOffset=std::uint16_t(first);c.edges.push_back(e);out.push_back(std::move(c));
}
void roadEdges(std::vector<NearTrackDrawCommand>& out,const NearTrackQueueState& q,unsigned end,unsigned first,unsigned side){
    for(unsigned cursor=end;cursor>first;cursor-=32){const auto& entry=q.entries.at(cursor/32);if(!(entry.edgeColor&0x80))lineCommand(out,entry.edges[side],entry.edgeColor,NearTrackSurface::road,end,first);}
}
void barrierEdges(std::vector<NearTrackDrawCommand>& out,const NearTrackQueueState& q,const NearTrackDrawInputs& in,unsigned end,unsigned first,unsigned side){
    for(unsigned cursor=end;cursor>first;cursor-=32){const auto& entry=q.entries.at(cursor/32);if(!(entry.edgeColor&0x80)&&entry.edgeColor!=3)lineCommand(out,entry.edges[side?5:4],in.sideEdgeColor,side?NearTrackSurface::leftSide:NearTrackSurface::rightSide,end,first);}
}
} // namespace

NearTrackEdge makeNearTrackEdge(NearTrackPoint from,NearTrackPoint to,int padding,int topPadding,int bottomPadding){
    from={sw(unsigned(from.x)),sw(unsigned(from.y))};to={sw(unsigned(to.x)),sw(unsigned(to.y))};
    NearTrackEdge e{};e.from=from;e.to=to;e.present=true;
    auto a=from,b=to;if(a.y<b.y){std::swap(a,b);e.clipFlags=0x40;}
    unsigned common=outcode(a,padding,topPadding,bottomPadding)&outcode(b,padding,topPadding,bottomPadding);
    if(!common){
        if(a.y<-topPadding)clipY(a,b,-topPadding);else if(a.y>=128+bottomPadding)clipY(a,b,128+bottomPadding);
        if(b.y<-topPadding)clipY(b,a,-topPadding);else if(b.y>=128+bottomPadding)clipY(b,a,128+bottomPadding);
        common=outcode(a,padding,topPadding,bottomPadding)&outcode(b,padding,topPadding,bottomPadding);
    }
    if(common)e.clipFlags=std::uint8_t(e.clipFlags|0x80|common);
    const int rasterY0=std::clamp(a.y,-topPadding,128+bottomPadding),rasterY1=std::clamp(b.y,-topPadding,128+bottomPadding);
    e.storageBytes=std::uint16_t(10+2*std::abs(rasterY0-rasterY1));
    e.clippedFrom={std::clamp(a.x,-padding,256+padding),std::clamp(a.y,-topPadding,128+bottomPadding)};
    e.clippedTo={std::clamp(b.x,-padding,256+padding),std::clamp(b.y,-topPadding,128+bottomPadding)};return e;
}
void seedNearTrackCutEdges(const NearTrackGeometryState& s,const std::array<NearTrackCutEdge,4>& cuts,NearTrackQueueState& q){
    for(unsigned n=0;n<4;++n){const auto& e=cuts[n];if(e.valid)storeEdge(q,32+4*n,makeNearTrackEdge(point(vertex(s,e.fromIndex)),point(vertex(s,e.toIndex)),q.horizontalPadding,q.topPadding,q.bottomPadding));else storeEdge(q,32+4*n,absent());}
}
void processNearTrackVisibility(NearTrackGeometryState& s,std::uint8_t renderFlags,NearTrackQueueState& q,
    const std::function<void(std::uint8_t)>& materialize){
    const int limit=s.progress.segment.limitDoubled;
    if(limit<4||limit>120||(limit&3))throw std::invalid_argument("Near visibility requires complete cross-sections");
    for(int side=0;side<2;++side){
        int index=limit+side*2-4;
        auto upper=[&]()->bool{return valid(s.upper[unsigned(index)/2]);};
        auto lower=[&]()->bool{return valid(s.lower[unsigned(index)/2]);};
        if(!upper())goto skipBack;
        if(lower())goto seekMissing;
        if((index&~2)==s.renderingLoopIndex){
            if(!s.progress.offRoadSide)goto skipBack;
            {const auto lateral=std::uint16_t((s.progress.lateralOffset<<2)|(s.progress.lateralOffset>>14));if(!((lateral^index)&2))goto skipBack;}
            goto force;
        }
        if(renderFlags&(side?0x40:0x80))goto force;
        goto skipBack;
    skipBack:
        index-=4;if(index<0)continue;
    scanValid:
        if(!upper()||!lower())goto skipBack;
        s.flags[unsigned(index)/4]|=0x40;
    seekMissing:
        index-=4;if(index<0)continue;
        if(!upper()||lower())goto seekMissing;
        if(index<s.renderingLoopIndex)continue;
    force:
        materialize(std::uint8_t(index));s.flags[unsigned(index)/4]|=0x40;
        index-=4;if(index>=0)goto scanValid;
        index+=4;if(q.offset==32)continue;
        {const unsigned offset=q.offset-16+(index?4u:0u);storeLine(q,offset,s,unsigned(index)+0x78,unsigned(index));}
    }
}
void appendNearTrackSegment(NearTrackGeometryState& s,NearTrackQueueState& q,bool first){
    const auto limit=s.progress.segment.limitDoubled;
    if(limit<4||limit>120||(limit&3))throw std::invalid_argument("Near queue requires complete cross-sections");
    if(s.progress.currentSegment>=q.coveredSegments.size())throw std::out_of_range("Near segment outside track");
    if(q.offset>=0x5e0)return;
    q.previousOffset=previousOffset(s,q);s.processedIndices[0]=0;int index=first?s.renderingLoopIndex:4;
    if(first){q.offset=48;if(index){for(int n=index-4;n>=0;n-=4)s.flags[unsigned(n)/4]=0x80;s.progress.depth=std::uint8_t(s.progress.depth+(unsigned(index)>>2)-1);}}
    else if(!q.previousOffset)manageBounds(q);
    bool immediate=first;
    for(;index<limit;index+=4,s.progress.depth=std::uint8_t(s.progress.depth+1)){
        if(immediate||valid(s.upper[unsigned(index)/2])){
            immediate=false;const unsigned previous=s.processedIndices[0];
            const bool alternate=(((unsigned(s.progress.segment.alternateFlag)<<1)^unsigned(index))&4)!=0;
            const auto flags=s.flags[unsigned(index)/4];adjustBounds(s,unsigned(index));
            if(q.offset!=48){
                storeLine(q,q.offset,s,unsigned(index),previous);q.offset+=4;
                storeLine(q,q.offset,s,unsigned(index)+2,previous+2);q.offset+=4;
                storeLine(q,q.offset,s,unsigned(index)+0x78,previous+0x78);q.offset+=4;
                storeLine(q,q.offset,s,unsigned(index)+0x7a,previous+0x7a);q.offset+=4;
            }
            if(alternate&&(flags&0x80)&&!(flags&0x40)){storeEdge(q,q.offset,absent());q.offset+=4;storeEdge(q,q.offset,absent());q.offset+=4;}
            else{storeLine(q,q.offset,s,unsigned(index),unsigned(index)+0x78);q.offset+=4;storeLine(q,q.offset,s,unsigned(index)+0x7a,unsigned(index)+2);q.offset+=4;}
            if(flags&0x80)storeEdge(q,q.offset,absent());else storeLine(q,q.offset,s,unsigned(index)+2,unsigned(index));q.offset+=4;
            auto& header=q.entries.at(q.offset/32);header.segment=s.progress.currentSegment;header.edgeColor=trackRoadEdgeColor(s.progress.depth,alternate);
            header.processedFlags=flags;header.steeringFlags=s.progress.segment.steeringFlags;q.offset+=4;
            q.coveredSegments[s.progress.currentSegment]=true;s.processedIndices[0]=std::uint8_t(index);
            if(q.offset>=0x5e0)return;
        }
        s.flags[unsigned(index)/4]=0x80;
        if(unsigned(index)>=q.previousOffset)manageBounds(q);
    }
}
std::vector<NearTrackDrawCommand> drawNearTrackQueue(const NearTrackQueueState& q,const NearTrackDrawInputs& in,const NearTrackPainter& painter,std::uint16_t* lineBytes){
    if(q.offset>q.entries.size()*32||(q.offset&31))throw std::invalid_argument("Near queue end is not a record boundary");
    std::vector<NearTrackDrawCommand> out;if(q.offset<96)return out;
    unsigned end=q.offset-32;
    while(end){
        unsigned first=end-32;while(first&&(q.entries.at(first/32).processedFlags&0x80))first-=32;
        if(first<in.minimumQueueOffset)break;
        const auto side=[&](unsigned which){auto r=fill(out,q,in,end,first,which?NearTrackSurface::leftSide:NearTrackSurface::rightSide,lineBytes);barrierEdges(out,q,in,end,first,which);return r.barrierOffset;};
        const auto road=[&](){fill(out,q,in,end,first,NearTrackSurface::road,lineBytes);if(q.entries.at(end/32).processedFlags&1)lineCommand(out,q.entries.at(end/32).edges[6],15,NearTrackSurface::road,end,first);};
        const auto car=[&](NearTrackPainterSlot slot){if(painter)painter(slot,std::uint16_t(first),out);};
        if(!in.renderingOrderMode){side(0);side(1);car(NearTrackPainterSlot::ahead);car(NearTrackPainterSlot::behind);road();roadEdges(out,q,end,first,0);roadEdges(out,q,end,first,1);car(NearTrackPainterSlot::occluded);}
        else if(sb(unsigned(in.offsetFromRoadCenter)>>8)<0){car(NearTrackPainterSlot::ahead);side(1);road();roadEdges(out,q,end,first,1);car(NearTrackPainterSlot::occluded);const auto barrier=side(0);roadEdges(out,q,in.raceStartTimer?end:barrier,first,0);car(NearTrackPainterSlot::behind);}
        else{car(NearTrackPainterSlot::behind);side(0);road();roadEdges(out,q,end,first,0);car(NearTrackPainterSlot::occluded);const auto barrier=side(1);roadEdges(out,q,in.raceStartTimer?end:barrier,first,1);car(NearTrackPainterSlot::ahead);}
        end=first;
    }
    return out;
}
} // namespace scr

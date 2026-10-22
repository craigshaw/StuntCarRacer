#include "native_track_preview.h"
#include "native_menu_render.h"
#include "native_race_initialization.h"
#include "planar_image.h"
#include "near_track_geometry.h"
#include "near_track_visibility.h"
#include "track_sides.h"
#include "state/track_progress_adapter.h"
#include "state/near_track_geometry_adapter.h"
#include "state/draw_bridge_adapter.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <functional>

namespace scr::viewer {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m[a])<<8)|m[a+1];}
struct RasterEdge {
    int high{},low{},first{},last{};
    std::vector<int> x;
};
// Original drawClippedLine's integer error accumulator. Each entry describes
// one row below high, with the bottom endpoint excluded from polygon coverage.
RasterEdge rasterEdge(const NearTrackEdge& e) {
    auto a=e.from,b=e.to;if(a.y<b.y)std::swap(a,b);
    RasterEdge r{a.y,b.y,a.x,b.x,{}};
    const int dx=std::abs(b.x-a.x),dy=a.y-b.y,step=b.x<a.x?-1:1;
    int x=a.x;
    const auto add=[&](unsigned& error,unsigned delta){const auto sum=error+delta;error=sum&65535;return sum>65535;};
    if(dx>=dy){
        unsigned error=65535u-unsigned(dx/2);
        for(int n=0;n<dx;++n){x+=step;if(add(error,unsigned(dy))){error=(error-unsigned(dx))&65535;r.x.push_back(x);}}
    }else{
        unsigned error=65535u-unsigned(dy/2);
        for(int n=0;n<dy;++n){r.x.push_back(x);if(add(error,unsigned(dx))){error=(error-unsigned(dy))&65535;x+=step;}}
    }
    return r;
}
void put(NativeMenuImage& image,int x,int y,unsigned color){
    // Original viewportTopAddress=displayFrameBuffer+0x284: x32,y16.
    if(x>=0&&x<256&&y>=0&&y<128)image.indices[std::size_t((y+16)*320+x+32)]=std::uint8_t(color);
}
void rasterCommand(NativeMenuImage& image,const NearTrackDrawCommand& command){
    if(command.kind==NearTrackCommandKind::fill){
        if(command.edges.empty())return;
        // Original initializeEdgeSortBuffers (0x68544) arranges a circular
        // contour, finds its local maxima and walks two descending chains.
        // Preserve its nested split order and reversed spans: a generic
        // even/odd fill changes a few pixels at banked, degenerate corners.
        auto edges=command.edges;
        const unsigned back=command.surface==NearTrackSurface::road?0:command.surface==NearTrackSurface::rightSide?2:1;
        auto split=std::find_if(edges.begin()+1,edges.end(),[&](const NearTrackEdge& e){return e.originEdge==back;});
        std::reverse(split,edges.end());
        std::vector<RasterEdge> raster;for(const auto& e:edges)raster.push_back(rasterEdge(e));
        if(raster.empty())return;
        struct Peak{int y;unsigned left,right;bool used{};};std::vector<Peak> peaks;
        unsigned direction=(edges[0].clipFlags&0x40)^0x40;
        int high=raster[0].high,low=raster[0].low;
        for(unsigned step=1;step<=raster.size();++step){
            const unsigned index=step%unsigned(raster.size());const auto& edge=raster[index];
            bool transition=false;
            if(edge.high==edge.low){if(index==0&&(edges[0].clipFlags&0x40))transition=direction!=0;}
            else if(edge.high==low)transition=direction!=0;
            else if(edge.low==high)direction=0x40;
            else transition=true;
            if(transition){direction^=0x40;if(!direction)peaks.push_back({edge.high,index,(index+unsigned(raster.size())-1)%unsigned(raster.size()),false});}
            high=edge.high;low=edge.low;
        }
        if(peaks.empty())return;
        auto select=[&](unsigned left,unsigned right){
            int selected=-1;
            for(int i=int(peaks.size())-1;i>=0;--i){const auto& p=peaks[unsigned(i)];
                const bool within=left<=right?p.right>=left&&p.right<=right:p.right>=left||p.right<=right;
                if(!p.used&&within&&(selected<0||p.y>peaks[unsigned(selected)].y))selected=i;
            }
            if(selected>=0)peaks[unsigned(selected)].used=true;return selected;
        };
        unsigned initial=unsigned(peaks.size()-1);
        for(int i=int(peaks.size())-2;i>=0;--i)if(peaks[unsigned(i)].y>peaks[initial].y)initial=unsigned(i);
        peaks[initial].used=true;
        struct Cursor{unsigned index{},sample{};};
        std::function<void(Cursor,Cursor,int,unsigned)> fill;
        fill=[&](Cursor left,Cursor right,int scan,unsigned depth){
            if(depth>raster.size())throw std::runtime_error("Preview polygon nesting exceeded edge count");
            if(scan<=0||scan>128)return;
            int next=select(left.index,right.index);
            for(unsigned budget=0;budget<1024;++budget){
                if(next>=0&&scan==peaks[unsigned(next)].y){
                    const auto peak=peaks[unsigned(next)];
                    fill(left,{peak.right,0},scan,depth+1);
                    left={peak.left,0};next=select(left.index,right.index);
                    continue;
                }
                unsigned guard=0;
                while(left.sample>=raster[left.index].x.size()){
                    left={(left.index+1)%unsigned(raster.size()),0};
                    if(left.index==right.index||raster[left.index].high!=scan||++guard>raster.size())return;
                }
                guard=0;
                while(right.sample>=raster[right.index].x.size()){
                    right={(right.index+unsigned(raster.size())-1)%unsigned(raster.size()),0};
                    if(left.index==right.index||raster[right.index].high!=scan||++guard>raster.size())return;
                }
                const auto x0=raster[left.index].x[left.sample++],x1=raster[right.index].x[right.sample++];
                for(int x=x0;x<x1;++x)put(image,x,scan-1,command.color);
                if(--scan<=0)return;
            }
            throw std::runtime_error("Preview polygon scan did not terminate");
        };
        fill({peaks[initial].left,0},{peaks[initial].right,0},peaks[initial].y,0);
    }else for(const auto& e:command.edges)if(e.present){
        const auto r=rasterEdge(e);
        int x=r.first,y=r.high-1;unsigned next=0;bool endpoint=false;
        const int direction=r.first>=r.last?-1:1;
        auto target=[&](){int result;
            if(next<r.x.size())result=r.x[next++];else{if(endpoint)return -32768;endpoint=true;result=r.last;}
            return result==0?-1:result;
        };
        int end=target();unsigned budget=0;
        while(y>=0&&y<128&&x>=0&&x<256&&end!=-32768){
            if(++budget>65536)throw std::runtime_error("Preview edge raster did not terminate");
            put(image,x,y,command.color);
            if(x==end){end=target();--y;if(x!=end)x+=direction;}else x+=direction;
        }
    }
}
NearTrackEdge line(const NearTrackVertex& a,const NearTrackVertex& b){
    if((a.height|b.height)&0x8000)return {};
    return makeNearTrackEdge({a.x,a.y},{b.x,b.y});
}
NearTrackQueueState previewQueue(NearTrackGeometryState& s){
    NearTrackQueueState q;q.offset=128;q.opponentSegment=255;
    auto& first=q.entries[3];first.segment=s.progress.currentSegment;first.edgeColor=9;
    first.steeringFlags=s.progress.segment.steeringFlags;
    first.edges[4]=line(s.upper[0],s.lower[0]);first.edges[5]=line(s.lower[1],s.upper[1]);first.edges[6]=line(s.upper[1],s.upper[0]);
    for(unsigned n=4;n<7;++n){first.edges[n].originQueueOffset=96;first.edges[n].originEdge=std::uint8_t(n);q.lineBytes=std::uint16_t(q.lineBytes+first.edges[n].storageBytes);}
    s.flags[0]=0x80;
    appendNearTrackSegment(s,q,false);return q;
}
}
struct NativeTrackPreview::State {
    NativeMemory memory{};
    TrackProgressData data;
    NativeMenuImage background;
    std::vector<std::uint32_t> pixels;
    std::vector<std::uint8_t> indices;
    std::uint8_t angle{},releaseMask{31};
    void render(){
        auto image=background;
        auto geometry=scr::state::nearGeometry::readState(memory);
        auto& p=geometry.progress;
        const unsigned index=angle&3;
        p.worldPosition[0]=(p.worldPosition[0]&0x00ffffff)|(std::uint32_t(memory[0x60552+index])<<24);
        p.worldPosition[2]=(p.worldPosition[2]&0x00ffffff)|(std::uint32_t(memory[0x60556+index])<<24);
        p.worldPosition[1]=(p.worldPosition[1]&0x0000ffff)|0x03f00000;
        TrackProgressInputs camera{};camera.yaw=std::uint16_t((word(memory,0x1bce6)&255)|(unsigned(memory[0x6055a+index])<<8));
        updateTrackCamera(camera,p);
        geometry.camera.viewportCenterY=0x700;
        geometry.camera.sinRoll=std::uint16_t(word(memory,0x1c250));geometry.camera.cosRoll=std::uint16_t(word(memory,0x1c252));
        geometry.displayUpdate=0x40;geometry.preview=0x80;geometry.edgeVisibilityMode=0x80;geometry.startIndex=0;geometry.renderingLoopIndex=0;geometry.continuation=0;
        for(int z=16;z>=0;--z)for(unsigned half=0;half<2;++half)for(int column=0;column<(half?9:8);++column){
            const int x=half?column-8:8-column;int gx=x,gz=z;
            switch(p.cameraRotation){case 0x4000:gx=z;gz=-x;break;case 0x8000:gx=-x;gz=-z;break;case 0xc000:gx=-z;gz=x;break;default:break;}
            const auto xx=std::uint8_t(p.viewX+gx),zz=std::uint8_t(p.viewZ+gz);if(xx>=16||zz>=16)continue;
            const auto segment=data.grid[unsigned(zz)*16+xx];if(segment==255)continue;
            geometry.gridSweepDirection=half?0x80:0;
            p.currentSegment=segment;p.visibility=0;p.depth=0;
            geometry.startIndex=0;geometry.processedIndices={0x80,0x80};
            projectNearTrackSegment(data,geometry);
            const unsigned last=geometry.progress.segment.maximumDoubled/2;
            const bool left=geometry.upper[0].y>geometry.upper[1].y||geometry.upper[8].y>geometry.upper[9].y||geometry.upper[last].y>geometry.upper[last+1].y;
            geometry.flags[0]=geometry.flags[geometry.progress.segment.maximumDoubled/4]=0;
            const auto queue=previewQueue(geometry);
            NearTrackDrawInputs drawing{};drawing.minimumQueueOffset=96;drawing.renderingOrderMode=0xe0;drawing.offsetFromRoadCenter=left?0:0x8000;
            drawing.sideEdgeColor=memory[0x1c9d0]?8:9;drawing.alternateSideColor=memory[0x1c9d0]?4:10;
            drawing.previewColorOverride=!(geometry.progress.segment.steeringFlags&0xc0)&&!(geometry.heightDifference&0x40);
            for(const auto& command:drawNearTrackQueue(queue,drawing))rasterCommand(image,command);
        }
        NativeMenuTextCursor instruction{};instruction.column=12;instruction.row=24;
        instruction.horizontalOffset=6;instruction.yOffset=8;instruction.colour=3;
        drawNativeMenuString(memory,image,instruction,0x64354,0x43);
        indices.assign(image.indices.begin(),image.indices.end());
        packNativeMenuPixels(image,pixels);
    }
};
NativeTrackPreview::NativeTrackPreview()=default;
NativeTrackPreview::~NativeTrackPreview()=default;
void NativeTrackPreview::load(const NativeMemory& memory,std::uint8_t angle){
    state_=std::make_unique<State>();auto& s=*state_;s.memory=memory;s.angle=angle;
    if(memory[0x1ca33]==5){
        // Original runTrackPreviewScreen resets the player configuration,
        // decodes the track, calls animateDrawBridge once, then builds its
        // render cache. Reset inside the owned copy so reloading or rotating
        // a caller's already animated race cannot advance the overview twice.
        initializeNativePlayerConfiguration(s.memory);
        initializeNativeTrack(s.memory,5);
        scr::state::drawBridge::animate(s.memory);
        rebuildNativeTrackGeometryCache(s.memory);
    }
    s.data=scr::state::trackProgress::readData(s.memory);
    const auto decoded=decodePlanarRle(memory.data()+0x2b994,memory.size()-0x2b994,320,200,4);
    const auto indexed=decodePlanarImage(decoded.planes.data(),decoded.planes.size(),320,200,4);
    std::copy(indexed.pixels.begin(),indexed.pixels.end(),s.background.indices.begin());
    for(unsigned n=0;n<16;++n)s.background.palette[n]=originalPaletteRgb(std::uint16_t(word(memory,0x2b974+n*2)));
    const unsigned track=memory[0x1ca33];if(track>7)throw std::invalid_argument("Preview track outside original database");
    s.memory[0x643ad]=memory[0x6129a+track];
    NativeMenuTextCursor title{};title.yOffset=12;title.colour=15;
    drawNativeMenuString(s.memory,s.background,title,0x64354,0x58);
    for(unsigned n=0;n<15;++n)drawNativeMenuCharacter(memory,s.background,title,memory[0x1edaa+track*16+n]);
    s.render();
}
NativeTrackPreviewAction NativeTrackPreview::input(std::uint8_t controls){
    if(!state_)throw std::logic_error("Track preview is not loaded");
    if(controls>31)throw std::invalid_argument("Track preview input exceeds digital mask");
    auto& s=*state_;s.releaseMask&=controls;if(s.releaseMask)return NativeTrackPreviewAction::none;
    if(controls&4)++s.angle;else if(controls&8)--s.angle;else if(controls&16)return NativeTrackPreviewAction::startRace;else return NativeTrackPreviewAction::none;
    s.releaseMask=31;s.render();return NativeTrackPreviewAction::redraw;
}
const std::vector<std::uint32_t>& NativeTrackPreview::pixels()const noexcept{static const std::vector<std::uint32_t> empty;return state_?state_->pixels:empty;}
const std::vector<std::uint8_t>& NativeTrackPreview::indices()const noexcept{static const std::vector<std::uint8_t> empty;return state_?state_->indices:empty;}
std::uint8_t NativeTrackPreview::angle()const noexcept{return state_?state_->angle:0;}
} // namespace scr::viewer

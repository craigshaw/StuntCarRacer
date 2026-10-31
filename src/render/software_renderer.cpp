#include "render/software_renderer.h"
#include "physics/motion_transforms.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scr {
namespace {
struct ScreenVertex {double x{},y{},inverseDepth{};};
struct RenderCanvas {
    int padding{};bool extended{};int bottomPadding{};
    int topPadding()const{return extended?16:0;}
    int width()const{return extended?viewportWidth():320+2*padding;}
    int viewportHeight()const{return 128+topPadding()+bottomPadding;}
    int viewportWidth()const{return 256+2*padding;}
    std::size_t viewIndex(int x,int y)const{return std::size_t(y*viewportWidth()+x);}
    std::size_t pixelIndex(int x,int y)const{return std::size_t((y+(extended?0:16))*width()+x+(extended?0:32));}
};
using Polygon=std::vector<RenderWorldPoint>;
constexpr double nearPlane=1.0;
int signedWord(std::uint16_t value){return value<0x8000?int(value):int(value)-65536;}
int floorShift(int value,unsigned bits){const int divisor=1<<bits;return value/divisor-(value%divisor<0);}
ScreenVertex horizonPoint(int side,const RenderProjectionCamera& camera,std::uint8_t mode){
    // Original renderHorizon uses fixed screen-angle endpoints +/-0x500,
    // independent of yaw. The signed visibility mode controls its eight-unit
    // vertical correction, as in original0x678a4.
    const auto down=std::uint16_t(-floorShift(signedWord(camera.viewportCenterY),3)-((mode&0x80)?0:8));
    const auto x=std::uint16_t(int(multiplyMatrixWord(std::uint16_t(side),camera.cosRoll))+multiplyMatrixWord(down,camera.sinRoll));
    const auto y=std::uint16_t(int(multiplyMatrixWord(down,camera.cosRoll))-multiplyMatrixWord(std::uint16_t(side),camera.sinRoll));
    return {double(floorShift(signedWord(x),2)+128),double(floorShift(signedWord(y),2)+64),0};
}
double edge(const ScreenVertex& a,const ScreenVertex& b,double x,double y) {
    return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);
}
RenderWorldPoint blend(const RenderWorldPoint& a,const RenderWorldPoint& b,double t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
Polygon clipNear(const Polygon& input,const RenderProjectionCamera& camera) {
    Polygon out;
    if(input.empty())return out;
    auto previous=input.back();auto previousZ=renderCameraRelative(previous,camera).forward;
    for(const auto& current:input){
        const auto currentZ=renderCameraRelative(current,camera).forward;
        if((previousZ>=nearPlane)!=(currentZ>=nearPlane))
            out.push_back(blend(previous,current,(nearPlane-previousZ)/(currentZ-previousZ)));
        if(currentZ>=nearPlane)out.push_back(current);
        previous=current;previousZ=currentZ;
    }
    return out;
}
void triangle(const ScreenVertex& a,const ScreenVertex& b,const ScreenVertex& c,std::uint32_t color,
    std::vector<std::uint32_t>& pixels,std::vector<float>& depth,std::vector<std::uint8_t>& coverage,
    std::uint8_t surface,RenderStatistics& stats,const RenderCanvas& canvas) {
    const double area=edge(a,b,c.x,c.y);if(std::abs(area)<0.001)return;
    const auto x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
    const auto y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y}))));
    const auto x1=std::min(canvas.viewportWidth()-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
    const auto y1=std::min(canvas.viewportHeight()-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
    if(x1<x0||y1<y0)return;
    ++stats.triangles;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const double wa=edge(b,c,x+0.5,y+0.5)/area;
        const double wb=edge(c,a,x+0.5,y+0.5)/area;
        const double wc=1-wa-wb;
        if(wa<-0.000001||wb<-0.000001||wc<-0.000001)continue;
        const double inverse=wa*a.inverseDepth+wb*b.inverseDepth+wc*c.inverseDepth;
        const auto index=canvas.viewIndex(x,y);
        if(inverse>depth[index]){
            depth[index]=float(inverse);
            coverage[index]=surface;pixels[canvas.pixelIndex(x,y)]=color;
        }
    }
}
// Liang-Barsky clips edge work to the recovered viewport. Edge depth is tested
// against the same buffer as surfaces, so distant outlines cannot draw through
// foreground track. Angular interpolation remains a presentation approximation.
void line(ScreenVertex a,ScreenVertex b,std::uint32_t color,std::vector<std::uint32_t>& pixels,
    const std::vector<float>& depth,const RenderCanvas& canvas,bool depthTest=true,std::vector<std::uint8_t>* highlights=nullptr) {
    const double dx=b.x-a.x,dy=b.y-a.y;double first=0,last=1;
    const auto clip=[&](double p,double q){
        if(std::abs(p)<1e-12)return q>=0;
        const double t=q/p;
        if(p<0){if(t>last)return false;first=std::max(first,t);}
        else{if(t<first)return false;last=std::min(last,t);}return true;
    };
    if(!clip(-dx,a.x)||!clip(dx,canvas.viewportWidth()-1-a.x)||!clip(-dy,a.y)||!clip(dy,canvas.viewportHeight()-1-a.y))return;
    const auto old=a;
    a={old.x+dx*first,old.y+dy*first,old.inverseDepth+(b.inverseDepth-old.inverseDepth)*first};
    b={old.x+dx*last,old.y+dy*last,old.inverseDepth+(b.inverseDepth-old.inverseDepth)*last};
    const int steps=std::max(1,int(std::ceil(std::max(std::abs(b.x-a.x),std::abs(b.y-a.y)))));
    for(int n=0;n<=steps;++n){
        const double t=double(n)/steps;const int x=int(std::round(a.x+(b.x-a.x)*t)),y=int(std::round(a.y+(b.y-a.y)*t));
        if(x<0||x>=canvas.viewportWidth()||y<0||y>=canvas.viewportHeight())continue;
        const double inverse=a.inverseDepth+(b.inverseDepth-a.inverseDepth)*t;
        if(!depthTest||inverse*1.015+0.000001>=depth[canvas.viewIndex(x,y)]){
            pixels[canvas.pixelIndex(x,y)]=color;
            if(highlights)(*highlights)[canvas.viewIndex(x,y)]=1;
        }
    }
}
// Some original mountain quads are concave. Fill their recovered boundary
// directly, so a fixed diagonal cannot paint across a notch between peaks.
void mountainPolygon(const MountainScreenPolygon& polygon,std::uint32_t color,
    std::vector<std::uint32_t>& pixels,std::vector<std::uint8_t>& coverage,const RenderCanvas& canvas) {
    int top=canvas.viewportHeight()-1,bottom=0;
    for(unsigned n=0;n<polygon.vertexCount;++n){top=std::min(top,polygon.vertices[n].y);bottom=std::max(bottom,polygon.vertices[n].y);}
    top=std::max(0,top);bottom=std::min(canvas.viewportHeight()-1,bottom);
    for(int y=top;y<=bottom;++y){
        const double scan=y+0.5;std::array<double,4> crossings{};unsigned count=0;
        for(unsigned n=0;n<polygon.vertexCount;++n){
            const auto& a=polygon.vertices[n];const auto& b=polygon.vertices[(n+1)%polygon.vertexCount];
            if((a.y>scan)!=(b.y>scan))crossings[count++]=a.x+(scan-a.y)*(double(b.x)-a.x)/(double(b.y)-a.y);
        }
        std::sort(crossings.begin(),crossings.begin()+count);
        for(unsigned n=0;n+1<count;n+=2){
            const int left=std::max(0,int(std::ceil(crossings[n]-0.5)));
            const int right=std::min(canvas.viewportWidth()-1,int(std::ceil(crossings[n+1]-0.5))-1);
            for(int x=left;x<=right;++x){pixels[canvas.pixelIndex(x,y)]=color;coverage[canvas.viewIndex(x,y)]=3;}
        }
    }
}
void nearPolygon(const NearTrackDrawCommand& command,std::uint32_t color,
    std::vector<std::uint32_t>& pixels,std::vector<std::uint8_t>& coverage,
    std::vector<std::uint8_t>& highlights,const RenderCanvas& canvas,std::vector<float>* depth) {
    struct Crossing {double x{},inverseDepth{};};
    std::vector<Crossing> crossings;crossings.reserve(command.edges.size());
    for(int y=0;y<canvas.viewportHeight();++y){
        crossings.clear();const double scan=y+0.5-canvas.topPadding();
        for(const auto& edge:command.edges)if(edge.present){
            const auto& a=edge.from;const auto& b=edge.to;
            if((a.y>scan)!=(b.y>scan)){
                const auto fraction=(scan-a.y)/(double(b.y)-a.y);
                const auto inverse=a.inverseDepth>0&&b.inverseDepth>0?a.inverseDepth+(b.inverseDepth-a.inverseDepth)*fraction:0;
                crossings.push_back({canvas.padding+a.x+(scan-a.y)*(double(b.x)-a.x)/(double(b.y)-a.y),inverse});
            }
        }
        std::sort(crossings.begin(),crossings.end(),[](const Crossing& a,const Crossing& b){return a.x<b.x;});
        for(std::size_t n=0;n+1<crossings.size();n+=2){
            const auto& left=crossings[n];const auto& right=crossings[n+1];
            const int first=std::max(0,int(std::ceil(left.x-0.5)));
            const int last=std::min(canvas.viewportWidth()-1,int(std::ceil(right.x-0.5))-1);
            for(int x=first;x<=last;++x){
                const auto index=canvas.viewIndex(x,y);pixels[canvas.pixelIndex(x,y)]=color;
                coverage[index]=command.surface==NearTrackSurface::opponent?4:command.surface==NearTrackSurface::road?1:2;highlights[index]=0;
                if(depth){
                    const auto fraction=right.x==left.x?0:(x+0.5-left.x)/(right.x-left.x);
                    (*depth)[index]=left.inverseDepth>0&&right.inverseDepth>0?
                        float(std::max(0.0,left.inverseDepth+(right.inverseDepth-left.inverseDepth)*fraction)):0;
                }
            }
        }
    }
}
} // namespace
RenderStatistics SoftwareRenderer::drawFrame(const TrackMesh& mesh,const std::vector<TrackSideFace>& sides,
    const MountainHorizonFrame& mountains,const RenderProjectionCamera& camera,
    const RenderProjectionTables& tables,const std::array<std::uint32_t,16>& palette,
    std::vector<std::uint32_t>& pixels,bool wireframe,const NearTrackPresentation* nearTrack,int padding,bool extended,int bottomPadding,bool captureTrackDepth) {
    if(padding<0||padding>304)throw std::invalid_argument("Invalid render viewport padding");
    const RenderCanvas canvas{padding,extended,bottomPadding};
    if(bottomPadding<0||canvas.viewportHeight()+(extended?0:16)>height)throw std::invalid_argument("Invalid render viewport height");
    if(pixels.size()!=std::size_t(canvas.width()*height))throw std::invalid_argument("Renderer background dimensions do not match its viewport");
    inverseDepth_.assign(canvas.viewportWidth()*canvas.viewportHeight(),0);
    coverage_.assign(canvas.viewportWidth()*canvas.viewportHeight(),0);
    highlights_.assign(canvas.viewportWidth()*canvas.viewportHeight(),0);
    const auto mode=nearTrack?nearTrack->horizonMode:std::uint8_t{};
    auto h0=horizonPoint(-0x500,camera,mode),h1=horizonPoint(0x500,camera,mode);
    h0.x+=padding;h1.x+=padding;h0.y+=canvas.topPadding();h1.y+=canvas.topPadding();
    for(int y=0;y<canvas.viewportHeight();++y)for(int x=0;x<canvas.viewportWidth();++x){
        // Original palette entries: sky7, ground13. The recovered mountain
        // polygons follow this horizon, before the depth-tested road and sides.
        const double side=(x-h0.x)*double(h1.y-h0.y)-(y-h0.y)*double(h1.x-h0.x);
        const bool sky=side>0;
        pixels[canvas.pixelIndex(x,y)]=palette[sky?7:13];
    }
    struct Face {std::vector<ScreenVertex> vertices;std::uint32_t color{};std::uint8_t surface{};};
    std::vector<Face> faces;RenderStatistics stats{};
    stats.mountainInstances=unsigned(mountains.instances.size());
    for(const auto& polygon:mountains.polygons){
        if(polygon.vertexCount<3||polygon.vertexCount>4||polygon.color>=palette.size())
            throw std::invalid_argument("Invalid mountain polygon");
        auto shifted=polygon;for(unsigned n=0;n<shifted.vertexCount;++n){shifted.vertices[n].x+=padding;shifted.vertices[n].y+=canvas.topPadding();}
        mountainPolygon(shifted,palette[polygon.color],pixels,coverage_,canvas);
        ++stats.mountainPolygons;
    }
    const auto projectPatch=[&](const Polygon& points,std::uint32_t color,std::uint8_t surface){
        const auto clipped=clipNear(points,camera);if(clipped.size()<3)return false;
        Face screen;screen.color=color;screen.surface=surface;
        for(const auto& point:clipped){
            const auto relative=renderCameraRelative(point,camera);const auto projected=projectRenderWorldPoint(point,camera,tables);
            screen.vertices.push_back({double(projected.x+padding),double(projected.y+canvas.topPadding()),1/std::max(nearPlane,relative.forward)});
        }
        faces.push_back(std::move(screen));return true;
    };
    const auto projectFace=[&](const Polygon& points,std::uint32_t color,std::uint8_t surface){
        double nearest=std::numeric_limits<double>::max(),farthest=-nearest;
        for(const auto& point:points){const auto z=renderCameraRelative(point,camera).forward;nearest=std::min(nearest,z);farthest=std::max(farthest,z);}
        if(farthest<nearPlane){++stats.clippedFaces;return;}
        // The original projection is angular, so a long straight world edge
        // can project to a curve. Close quads need intermediate samples before
        // near clipping; joining their two extreme corners can leave a gap
        // under the car. This tessellation is a native presentation choice.
        const unsigned divisions=nearest<48?8:1;bool visible=false;
        const auto sample=[&](unsigned u,unsigned v){
            const auto a=blend(points[0],points[1],double(u)/divisions);
            const auto b=blend(points[3],points[2],double(u)/divisions);
            return blend(a,b,double(v)/divisions);
        };
        for(unsigned u=0;u<divisions;++u)for(unsigned v=0;v<divisions;++v)
            visible=projectPatch({sample(u,v),sample(u+1,v),sample(u+1,v+1),sample(u,v+1)},color,surface)||visible;
        if(visible){if(surface==1)++stats.roadFaces;else ++stats.sideFaces;}else ++stats.clippedFaces;
    };
    for(const auto& face:mesh.faces){
        if(nearTrack&&nearTrack->coveredSegments.at(face.segment))continue;
        Polygon points;
        for(auto index:face.indices){if(index>=mesh.vertices.size())throw std::out_of_range("Road face index outside mesh");const auto& v=mesh.vertices[index];points.push_back({v.x,v.y,v.z});}
        projectFace(points,palette[trackRoadFillColor(face.segment,face.flags)],1);
    }
    for(const auto& face:sides){
        if(nearTrack&&nearTrack->coveredSegments.at(face.segment))continue;
        if(face.fillColor>=palette.size())throw std::invalid_argument("Invalid sidewall palette index");
        Polygon points;for(const auto& v:face.vertices)points.push_back({v.x,v.y,v.z});
        projectFace(points,palette[face.fillColor],2);
    }
    for(const auto& face:faces)for(std::size_t n=1;n+1<face.vertices.size();++n)
        triangle(face.vertices[0],face.vertices[n],face.vertices[n+1],face.color,pixels,inverseDepth_,coverage_,face.surface,stats,canvas);
    // The original far strip has filled panels, without generic black outlines.
    // Marked transverse edges are white. The near renderer's alternating yellow
    // and brown longitudinal lines require its visibility queue, not distance
    // guesses; keep those separate from this native depth-buffer presentation.
    for(const auto& face:mesh.faces)if(trackSurfaceCrossEdge(face.flags)&&(!nearTrack||!nearTrack->coveredSegments.at(face.segment))){
        const auto& a=mesh.vertices.at(face.indices[1]);const auto& b=mesh.vertices.at(face.indices[2]);
        RenderWorldPoint first{a.x,a.y,a.z},last{b.x,b.y,b.z};
        const auto firstZ=renderCameraRelative(first,camera).forward,lastZ=renderCameraRelative(last,camera).forward;
        if(firstZ<nearPlane&&lastZ<nearPlane)continue;
        if((firstZ<nearPlane)!=(lastZ<nearPlane)){
            const auto intersection=blend(first,last,(nearPlane-firstZ)/(lastZ-firstZ));
            if(firstZ<nearPlane)first=intersection;else last=intersection;
        }
        const auto endpoint=[&](const RenderWorldPoint& point){const auto p=projectRenderWorldPoint(point,camera,tables);
            return ScreenVertex{double(p.x+padding),double(p.y+canvas.topPadding()),1/std::max(nearPlane,renderCameraRelative(point,camera).forward)};};
        line(endpoint(first),endpoint(last),palette[15],pixels,inverseDepth_,canvas);
    }
    if(wireframe)for(const auto& face:faces){
        for(std::size_t n=0;n<face.vertices.size();++n)
            line(face.vertices[n],face.vertices[(n+1)%face.vertices.size()],palette[3],pixels,inverseDepth_,canvas);
        for(std::size_t n=2;n+1<face.vertices.size();++n)
            line(face.vertices[0],face.vertices[n],palette[3],pixels,inverseDepth_,canvas);
    }
    if(nearTrack){
        stats.nearQueueEntries=nearTrack->queueEntries;
        const auto drawEdge=[&](const NearTrackEdge& edge,std::uint32_t color,bool highlight){
            if(!edge.present)return;
            line({double(edge.from.x+padding),double(edge.from.y+canvas.topPadding()),0},{double(edge.to.x+padding),double(edge.to.y+canvas.topPadding()),0},
                color,pixels,inverseDepth_,canvas,false,highlight?&highlights_:nullptr);
        };
        for(const auto& command:nearTrack->commands){
            if(command.color>=palette.size())throw std::invalid_argument("Near command palette index outside range");
            if(command.kind==NearTrackCommandKind::fill){
                nearPolygon(command,palette[command.color],pixels,coverage_,highlights_,canvas,captureTrackDepth?&inverseDepth_:nullptr);++stats.nearFillCommands;
                stats.opponentPolygons+=command.surface==NearTrackSurface::opponent;
                if(wireframe)for(const auto& edge:command.edges)drawEdge(edge,palette[3],false);
            }else{
                for(const auto& edge:command.edges)drawEdge(edge,palette[command.color],true);
                ++stats.edgeHighlightCommands;
            }
        }
    }
    stats.roadPixels=unsigned(std::count(coverage_.begin(),coverage_.end(),1));
    stats.sidePixels=unsigned(std::count(coverage_.begin(),coverage_.end(),2));
    stats.mountainPixels=unsigned(std::count(coverage_.begin(),coverage_.end(),3));
    stats.edgeHighlightPixels=unsigned(std::count(highlights_.begin(),highlights_.end(),1));
    stats.opponentPixels=unsigned(std::count(coverage_.begin(),coverage_.end(),4));
    return stats;
}
} // namespace scr

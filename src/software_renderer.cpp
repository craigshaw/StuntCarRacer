#include "software_renderer.h"
#include "motion_transforms.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scr {
namespace {
struct ScreenVertex {double x{},y{},inverseDepth{};};
using Polygon=std::vector<RenderWorldPoint>;
constexpr double nearPlane=1.0;
int signedWord(std::uint16_t value){return value<0x8000?int(value):int(value)-65536;}
int floorShift(int value,unsigned bits){const int divisor=1<<bits;return value/divisor-(value%divisor<0);}
ScreenVertex horizonPoint(int side,const RenderProjectionCamera& camera){
    // Original renderHorizon uses fixed screen-angle endpoints +/-0x500,
    // independent of yaw. Practice mode lowers its vertical angle by eight.
    const auto down=std::uint16_t(-floorShift(signedWord(camera.viewportCenterY),3)-8);
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
    std::vector<std::uint32_t>& pixels,std::vector<float>& depth,RenderStatistics& stats) {
    const double area=edge(a,b,c.x,c.y);if(std::abs(area)<0.001)return;
    const auto x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
    const auto y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y}))));
    const auto x1=std::min(255,int(std::ceil(std::max({a.x,b.x,c.x}))));
    const auto y1=std::min(127,int(std::ceil(std::max({a.y,b.y,c.y}))));
    if(x1<x0||y1<y0)return;
    ++stats.triangles;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const double wa=edge(b,c,x+0.5,y+0.5)/area;
        const double wb=edge(c,a,x+0.5,y+0.5)/area;
        const double wc=1-wa-wb;
        if(wa<-0.000001||wb<-0.000001||wc<-0.000001)continue;
        const double inverse=wa*a.inverseDepth+wb*b.inverseDepth+wc*c.inverseDepth;
        const auto index=std::size_t(y*256+x);
        if(inverse>depth[index]){
            depth[index]=float(inverse);pixels[std::size_t((y+16)*320+x+32)]=color;
        }
    }
}
// Liang-Barsky clips edge work to the recovered viewport. Edge depth is tested
// against the same buffer as surfaces, so distant outlines cannot draw through
// foreground track. Angular interpolation remains a presentation approximation.
void line(ScreenVertex a,ScreenVertex b,std::uint32_t color,std::vector<std::uint32_t>& pixels,
    const std::vector<float>& depth) {
    const double dx=b.x-a.x,dy=b.y-a.y;double first=0,last=1;
    const auto clip=[&](double p,double q){
        if(std::abs(p)<1e-12)return q>=0;
        const double t=q/p;
        if(p<0){if(t>last)return false;first=std::max(first,t);}
        else{if(t<first)return false;last=std::min(last,t);}return true;
    };
    if(!clip(-dx,a.x)||!clip(dx,255-a.x)||!clip(-dy,a.y)||!clip(dy,127-a.y))return;
    const auto old=a;
    a={old.x+dx*first,old.y+dy*first,old.inverseDepth+(b.inverseDepth-old.inverseDepth)*first};
    b={old.x+dx*last,old.y+dy*last,old.inverseDepth+(b.inverseDepth-old.inverseDepth)*last};
    const int steps=std::max(1,int(std::ceil(std::max(std::abs(b.x-a.x),std::abs(b.y-a.y)))));
    for(int n=0;n<=steps;++n){
        const double t=double(n)/steps;const int x=int(std::round(a.x+(b.x-a.x)*t)),y=int(std::round(a.y+(b.y-a.y)*t));
        if(x<0||x>255||y<0||y>127)continue;
        const double inverse=a.inverseDepth+(b.inverseDepth-a.inverseDepth)*t;
        if(inverse*1.015+0.000001>=depth[std::size_t(y*256+x)])pixels[std::size_t((y+16)*320+x+32)]=color;
    }
}
} // namespace
RenderStatistics SoftwareReplayRenderer::drawTrack(const TrackMesh& mesh,const RenderProjectionCamera& camera,
    const RenderProjectionTables& tables,const std::array<std::uint32_t,16>& palette,
    std::vector<std::uint32_t>& pixels,bool wireframe) {
    if(pixels.size()!=std::size_t(width*height))throw std::invalid_argument("Renderer requires a320x200 background");
    inverseDepth_.assign(viewportWidth*viewportHeight,0);
    const auto h0=horizonPoint(-0x500,camera),h1=horizonPoint(0x500,camera);
    for(int y=0;y<viewportHeight;++y)for(int x=0;x<viewportWidth;++x){
        // Original palette entries: sky7, ground13, road1/2. Horizon coverage is
        // a simple half-plane; the original mountain/visibility queues follow
        // in later renderer recovery. Roll and pitch use recovered projection.
        const double side=(x-h0.x)*double(h1.y-h0.y)-(y-h0.y)*double(h1.x-h0.x);
        const bool sky=side>0;
        pixels[std::size_t((y+viewportY)*width+x+viewportX)]=palette[sky?7:13];
    }
    struct Face {std::vector<ScreenVertex> vertices;std::uint32_t color{};};
    std::vector<Face> faces;RenderStatistics stats{};
    for(const auto& face:mesh.faces){
        Polygon points;
        for(auto index:face.indices){if(index>=mesh.vertices.size())throw std::out_of_range("Road face index outside mesh");const auto& v=mesh.vertices[index];points.push_back({v.x,v.y,v.z});}
        auto clipped=clipNear(points,camera);if(clipped.size()<3){++stats.clippedFaces;continue;}
        Face screen;screen.color=palette[(face.flags&1)?2:1];
        for(const auto& point:clipped){
            const auto relative=renderCameraRelative(point,camera);const auto projected=projectRenderWorldPoint(point,camera,tables);
            screen.vertices.push_back({double(projected.x),double(projected.y),1/std::max(nearPlane,relative.forward)});
        }
        ++stats.roadFaces;faces.push_back(std::move(screen));
    }
    for(const auto& face:faces)for(std::size_t n=1;n+1<face.vertices.size();++n)
        triangle(face.vertices[0],face.vertices[n],face.vertices[n+1],face.color,pixels,inverseDepth_,stats);
    for(const auto& face:faces)for(std::size_t n=0;n<face.vertices.size();++n)
        line(face.vertices[n],face.vertices[(n+1)%face.vertices.size()],palette[wireframe?3:0],pixels,inverseDepth_);
    if(wireframe)for(const auto& face:faces)for(std::size_t n=2;n+1<face.vertices.size();++n)
        line(face.vertices[0],face.vertices[n],palette[3],pixels,inverseDepth_);
    stats.roadPixels=unsigned(std::count_if(inverseDepth_.begin(),inverseDepth_.end(),[](float value){return value>0;}));
    return stats;
}
} // namespace scr

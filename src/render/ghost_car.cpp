#include "render/ghost_car.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr {
namespace {
constexpr double pi=3.14159265358979323846,nearPlane=1;
double worldCoordinate(std::uint32_t value) noexcept{
    return (value<0x80000000u?double(value):double(std::int64_t(value)-0x100000000LL))/65536;
}
struct Vertex {double x{},y{},inverseDepth{};};
using Polygon=std::vector<RenderWorldPoint>;
RenderWorldPoint mix(const RenderWorldPoint& a,const RenderWorldPoint& b,double t){
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
double distance(const RenderWorldPoint& a,const RenderWorldPoint& b){
    return std::hypot(std::hypot(a.x-b.x,a.z-b.z),a.y-b.y);
}
void box(GhostCarMesh& mesh,const RenderWorldPoint& low,const RenderWorldPoint& high,std::uint32_t color){
    const auto first=unsigned(mesh.vertices.size());
    for(unsigned n=0;n<8;++n)mesh.vertices.push_back({n&1?high.x:low.x,n&2?high.y:low.y,n&4?high.z:low.z});
    constexpr unsigned faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(const auto& face:faces){
        GhostCarFace f;f.color=color;
        for(unsigned n=0;n<4;++n)f.vertices[n]=first+face[n];
        mesh.faces.push_back(f);
    }
}
Polygon clip(const Polygon& input,const RenderProjectionCamera& camera){
    Polygon out;if(input.empty())return out;
    auto a=input.back();auto za=renderCameraRelative(a,camera).forward;
    for(const auto& b:input){
        const auto zb=renderCameraRelative(b,camera).forward;
        if((za>=nearPlane)!=(zb>=nearPlane))out.push_back(mix(a,b,(nearPlane-za)/(zb-za)));
        if(zb>=nearPlane)out.push_back(b);
        a=b;za=zb;
    }
    return out;
}
double edge(const Vertex& a,const Vertex& b,double x,double y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);}
void triangle(const Vertex& a,const Vertex& b,const Vertex& c,int width,int height,
    std::vector<float>& depth,std::vector<std::uint32_t>* colors,std::uint32_t color){
    const auto area=edge(a,b,c.x,c.y);if(std::abs(area)<0.0001)return;
    const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
    const int x1=std::min(width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
    const int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y}))));
    const int y1=std::min(height-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        const auto wa=edge(b,c,x+0.5,y+0.5)/area,wb=edge(c,a,x+0.5,y+0.5)/area,wc=1-wa-wb;
        if(wa<-0.000001||wb<-0.000001||wc<-0.000001)continue;
        const auto inverse=wa*a.inverseDepth+wb*b.inverseDepth+wc*c.inverseDepth;
        const auto at=std::size_t(y*width+x);
        if(inverse>depth[at]){depth[at]=float(inverse);if(colors)(*colors)[at]=color;}
    }
}
void polygon(const Polygon& points,const RenderProjectionCamera& camera,const RenderProjectionTables& tables,
    int width,int height,int padding,int top,std::vector<float>& depth,
    std::vector<std::uint32_t>* colors,std::uint32_t color,unsigned& triangles){
    const auto clipped=clip(points,camera);if(clipped.size()<3)return;
    std::vector<Vertex> screen;screen.reserve(clipped.size());
    for(const auto& p:clipped){
        const auto projected=projectRenderWorldPoint(p,camera,tables);
        screen.push_back({double(projected.x+padding),double(projected.y+top),
            1/std::max(nearPlane,renderCameraRelative(p,camera).forward)});
    }
    for(unsigned n=1;n+1<screen.size();++n){
        triangle(screen[0],screen[n],screen[n+1],width,height,depth,colors,color);++triangles;
    }
}
// Angular projection bends world edges. Subdivide both auxiliary track quads and
// the car before near clipping; a conventional four-corner depth pass leaves
// gaps near the camera. This is an explicit proof approximation, not a change
// to the recovered world painter.
void quad(const std::array<RenderWorldPoint,4>& p,const RenderProjectionCamera& camera,
    const RenderProjectionTables& tables,int width,int height,int padding,int top,
    std::vector<float>& depth,std::vector<std::uint32_t>* colors,std::uint32_t color,unsigned& triangles){
    double farthest=-1;for(const auto& v:p)farthest=std::max(farthest,renderCameraRelative(v,camera).forward);
    if(farthest<nearPlane)return;
    const auto steps=[&](double length){return unsigned(std::clamp(std::ceil(length/(colors?2.0:8.0)),1.0,16.0));};
    const auto uSteps=steps(std::max(distance(p[0],p[1]),distance(p[3],p[2])));
    const auto vSteps=steps(std::max(distance(p[0],p[3]),distance(p[1],p[2])));
    const auto sample=[&](unsigned u,unsigned v){return mix(mix(p[0],p[1],double(u)/uSteps),mix(p[3],p[2],double(u)/uSteps),double(v)/vSteps);};
    for(unsigned u=0;u<uSteps;++u)for(unsigned v=0;v<vSteps;++v)
        polygon({sample(u,v),sample(u+1,v),sample(u+1,v+1),sample(u,v+1)},camera,tables,
            width,height,padding,top,depth,colors,color,triangles);
}
std::uint32_t blend(std::uint32_t background,std::uint32_t foreground,double opacity){
    std::uint32_t color=0;
    for(unsigned shift:{0u,8u,16u})color|=std::uint32_t(std::lround(double((background>>shift)&255)*(1-opacity)+double((foreground>>shift)&255)*opacity))<<shift;
    return color;
}
} // namespace
GhostCarPose makeGhostCarPose(const RenderCameraInputs& pose) noexcept{
    return {{worldCoordinate(pose.worldPosition[0]),worldCoordinate(pose.worldPosition[1]),worldCoordinate(pose.worldPosition[2])},pose.pitch,pose.yaw,pose.roll};
}
GhostCarMesh buildGhostCarMesh(const GhostCarPose& pose){
    if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||!std::isfinite(pose.position.z))
        throw std::invalid_argument("Ghost position must be finite");
    GhostCarMesh mesh;
    // The old proof guessed width from player height lever arms. The recovered
    // opponent instead puts the body within the middle half of its projected
    // footprint, with each tyre occupying one quarter. Match that silhouette
    // at flat-support rear/three-quarter observations (see the proof CSV).
    constexpr double width=ghostReferenceWidth,length=ghostReferenceFootprintLength;
    constexpr double contact=1.25,tyreWidth=width/4,tyreHeight=width/2,radius=tyreHeight/2;
    // Counterparts of the recovered procedural body vertices 4..11: a
    // sloping upper panel, half-footprint lower width and 3/8-width high end.
    // Convert the original screen-height offsets using the game's half-height
    // world projection. No extra cabin box or nose is inferred from physics.
    constexpr double support=contact+tyreHeight,low=support-width/4,high=support+width/2;
    mesh.vertices={{-width/4,low,length/2},{-width*3/16,high,length/2},
        {width*3/16,high,length/2},{width/4,low,length/2},
        {-width/4,low,-length/2},{-width/4,support,-length/2},
        {width/4,support,-length/2},{width/4,low,-length/2}};
    mesh.faces={{{0,3,2,1},0x9ff5ff},{{4,5,6,7},0x55d9ef},
        {{1,2,6,5},0x9ff5ff},{{0,1,5,4},0x55d9ef},
        {{3,7,6,2},0x55d9ef},{{0,4,7,3},0x55d9ef}};
    // The original footprint rows bound the screen-space tyres; they are not
    // centres of four circular 3D wheels. Keep the tyre ends within those rows.
    for(double x:{-width*3/8,width*3/8})for(double z:{-length/2+radius,length/2-radius})
        box(mesh,{x-tyreWidth/2,contact,z-radius},{x+tyreWidth/2,contact+tyreHeight,z+radius},0x286779);
    const auto radians=[](std::uint16_t angle){return double(angle)*2*pi/65536;};
    const auto sy=std::sin(radians(pose.yaw)),cy=std::cos(radians(pose.yaw));
    const auto sp=std::sin(radians(pose.pitch)),cp=std::cos(radians(pose.pitch));
    const auto sr=std::sin(radians(pose.roll)),cr=std::cos(radians(pose.roll));
    for(auto& p:mesh.vertices){
        // Positive pitch raises the front; positive roll raises the left wheel,
        // as in calculateExpectedWheelHeights. Yaw zero faces world +Z.
        const auto side=p.x*cr+p.y*sr,up=p.y*cr-p.x*sr;
        const auto vertical=up*cp+p.z*sp,forward=p.z*cp-up*sp;
        p={pose.position.x+side*cy+forward*sy,pose.position.y+vertical,pose.position.z+forward*cy-side*sy};
    }
    return mesh;
}
GhostCarStatistics GhostCarRenderer::draw(const GhostCarPose& pose,const GhostCarStyle& style,
    const TrackMesh& mesh,const std::vector<TrackSideFace>& sides,
    const RenderProjectionCamera& camera,const RenderProjectionTables& tables,
    std::vector<std::uint32_t>& pixels,const std::vector<std::uint8_t>& coverage,int padding,bool extended,int bottom,const std::vector<float>* paintedDepth){
    if(padding<0||padding>304||bottom<0||bottom>56||(!extended&&(padding||bottom))||
       !std::isfinite(style.opacity)||style.opacity<0||style.opacity>1||
       !std::isfinite(style.fadeNear)||!std::isfinite(style.fadeFar)||style.fadeNear<0||style.fadeFar<=style.fadeNear)
        throw std::invalid_argument("Invalid ghost presentation settings");
    const int width=256+2*padding,top=extended?16:0,height=128+top+bottom;
    const int canvasWidth=extended?width:320,offsetX=extended?0:32,offsetY=extended?0:16;
    if(pixels.size()!=std::size_t(canvasWidth*200)||coverage.size()!=std::size_t(width*height))
        throw std::invalid_argument("Ghost buffers do not match viewport");
    if(paintedDepth&&paintedDepth->size()!=coverage.size())throw std::invalid_argument("Painted depth does not match viewport");
    const auto car=buildGhostCarMesh(pose);GhostCarStatistics stats;
    // Proximity follows the player's physical reference, not the recovered
    // speed-dependent camera baseline, which can rise far above the vehicle.
    const RenderWorldPoint player{worldCoordinate(camera.worldPosition[0]),worldCoordinate(camera.worldPosition[1]),worldCoordinate(camera.worldPosition[2])};
    stats.effectiveOpacity=style.opacity*std::clamp((distance(player,pose.position)-style.fadeNear)/(style.fadeFar-style.fadeNear),0.0,1.0);
    if(!stats.effectiveOpacity){
        if(paintedDepth&&style.occlusion)for(std::size_t n=0;n<coverage.size();++n)
            stats.trackPixelsWithoutDepth+=(coverage[n]==1||coverage[n]==2)&&(*paintedDepth)[n]<=0;
        return stats;
    }
    worldDepth_.assign(coverage.size(),0);carDepth_.assign(coverage.size(),0);carColor_.assign(coverage.size(),0);
    unsigned worldTriangles=0;
    if(style.occlusion&&paintedDepth)worldDepth_=*paintedDepth;
    if(style.occlusion&&!paintedDepth){
        for(const auto& face:mesh.faces){
            std::array<RenderWorldPoint,4> points;
            for(unsigned n=0;n<4;++n){const auto& v=mesh.vertices.at(face.indices[n]);points[n]={v.x,v.y,v.z};}
            quad(points,camera,tables,width,height,padding,top,worldDepth_,nullptr,0,worldTriangles);
        }
        for(const auto& face:sides){
            std::array<RenderWorldPoint,4> points;
            for(unsigned n=0;n<4;++n){const auto& v=face.vertices[n];points[n]={v.x,v.y,v.z};}
            quad(points,camera,tables,width,height,padding,top,worldDepth_,nullptr,0,worldTriangles);
        }
    }
    for(const auto& face:car.faces){
        std::array<RenderWorldPoint,4> points;for(unsigned n=0;n<4;++n)points[n]=car.vertices.at(face.vertices[n]);
        quad(points,camera,tables,width,height,padding,top,carDepth_,&carColor_,face.color,stats.triangles);
    }
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto at=std::size_t(y*width+x);const bool track=coverage[at]==1||coverage[at]==2;
        stats.trackPixelsWithoutDepth+=track&&style.occlusion&&!worldDepth_[at];
        if(!carDepth_[at])continue;
        stats.ghostPixelsWithoutTrackDepth+=track&&style.occlusion&&!worldDepth_[at];
        ++stats.silhouettePixels;
        // Restrict the auxiliary mask to road/side pixels actually painted by
        // the original command stream. Never mask a ghost against an unseen
        // generic-mesh surface. Small depth tolerance handles raster rounding.
        // Unknown painted-track depth is conservative: hide that fragment
        // rather than letting it draw through an original fill boundary.
        if(style.occlusion&&track&&(!worldDepth_[at]||worldDepth_[at]>carDepth_[at]*1.002f+0.000001f)){++stats.occludedPixels;continue;}
        const auto pixel=std::size_t((y+offsetY)*canvasWidth+x+offsetX);
        pixels[pixel]=blend(pixels[pixel],carColor_[at],stats.effectiveOpacity);++stats.visiblePixels;
    }
    return stats;
}
} // namespace scr

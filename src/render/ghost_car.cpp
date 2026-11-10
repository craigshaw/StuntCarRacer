#include "render/ghost_car.h"
#include "render/opponent_car.h"
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
    // The physical datum sinks as suspension load increases. The original
    // cockpit compensates using this same retained suspension response word.
    // Our small exterior was calibrated at 1.25 world units of load; preserve
    // that silhouette, adding the excess allowance rather than burying it.
    // Zero response in flight keeps the recorded airborne datum unchanged.
    constexpr double rest=1.25;
    const auto load=pose.averageWheelVelocity<0x8000?std::min(unsigned(pose.averageWheelVelocity),0x11ffu):0u;
    const auto lift=std::max(0.0,double(load)/256-rest);
    return {{worldCoordinate(pose.worldPosition[0]),worldCoordinate(pose.worldPosition[1]),worldCoordinate(pose.worldPosition[2])},pose.pitch,pose.yaw,pose.roll,lift};
}
GhostCarMesh buildGhostCarMesh(const GhostCarPose& pose){
    if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||!std::isfinite(pose.position.z)||!std::isfinite(pose.suspensionLift)||pose.suspensionLift<0)
        throw std::invalid_argument("Ghost position must be finite");
    GhostCarMesh mesh;
    // The old proof guessed width from player height lever arms. The recovered
    // opponent instead puts the body within the middle half of its projected
    // footprint, with each tyre occupying one quarter. Match that silhouette
    // at flat-support rear/three-quarter observations (see the proof CSV).
    constexpr double width=ghostReferenceWidth,length=ghostReferenceFootprintLength;
    constexpr double contact=1.25,tyreHeight=width/2;
    // Counterparts of the recovered procedural body vertices 4..11: a
    // sloping upper panel, half-footprint lower width and 3/8-width high end.
    // Local +Z is the nose: the low/thin end goes forward, the tall end aft.
    // Convert the original screen-height offsets using the game's half-height
    // world projection. No extra cabin box or nose is inferred from physics.
    constexpr double support=contact+tyreHeight,low=support-width/4,high=support+width/2;
    mesh.vertices={{-width/4,low,-length/2},{-width*3/16,high,-length/2},
        {width*3/16,high,-length/2},{width/4,low,-length/2},
        {-width/4,low,length/2},{-width/4,support,length/2},
        {width/4,support,length/2},{width/4,low,length/2}};
    mesh.faces={{{0,3,2,1},0x9ff5ff},{{4,5,6,7},0x55d9ef},
        {{1,2,6,5},0x9ff5ff},{{0,1,5,4},0x55d9ef},
        {{3,7,6,2},0x55d9ef},{{0,4,7,3},0x55d9ef}};
    // Match the opponent's projected tyre construction. Extruding these rows
    // into boxes makes them disproportionately large at oblique angles.
    mesh.tyreRows={{{{{-width/2,support,length/2},{width/2,support,length/2}}},
                    {{{-width/2,support,-length/2},{width/2,support,-length/2}}}}};
    const auto radians=[](std::uint16_t angle){return double(angle)*2*pi/65536;};
    const auto sy=std::sin(radians(pose.yaw)),cy=std::cos(radians(pose.yaw));
    const auto sp=std::sin(radians(pose.pitch)),cp=std::cos(radians(pose.pitch));
    const auto sr=std::sin(radians(pose.roll)),cr=std::cos(radians(pose.roll));
    const auto transform=[&](RenderWorldPoint& p){
        // The original projection halves world Y. Rotate in its isotropic
        // space, then restore world Y, so a recorded 20-degree pitch/roll also
        // looks like 20 degrees. Flat proportions remain unchanged. Positive
        // pitch raises the front; positive roll raises the left. Yaw zero +Z.
        const auto side=p.x*cr+(p.y/2)*sr,up=(p.y/2)*cr-p.x*sr;
        const auto vertical=up*cp+p.z*sp,forward=p.z*cp-up*sp;
        p={pose.position.x+side*cy+forward*sy,pose.position.y+pose.suspensionLift+vertical*2,pose.position.z+forward*cy-side*sy};
    };
    for(auto& p:mesh.vertices)transform(p);
    for(auto& row:mesh.tyreRows)for(auto& p:row)transform(p);
    return mesh;
}
GhostCarRenderer::Raster GhostCarRenderer::rasterize(const GhostCarPose& pose,const GhostCarStyle& style,
    const RenderProjectionCamera& camera,const RenderProjectionTables& tables,
    int padding,bool extended,int bottom){
    if(padding<0||padding>304||bottom<0||bottom>56||(!extended&&(padding||bottom))||
       !std::isfinite(style.opacity)||style.opacity<0||style.opacity>1||
       !std::isfinite(style.fadeNear)||!std::isfinite(style.fadeFar)||style.fadeNear<0||style.fadeFar<=style.fadeNear)
        throw std::invalid_argument("Invalid ghost presentation settings");
    const int width=256+2*padding,top=extended?16:0,height=128+top+bottom;
    Raster raster;
    raster.width=width;raster.height=height;raster.canvasWidth=extended?width:320;
    raster.offsetX=extended?0:32;raster.offsetY=extended?0:16;
    auto& stats=raster.statistics;
    const auto car=buildGhostCarMesh(pose);
    // Proximity follows the player's physical reference, not the recovered
    // speed-dependent camera baseline, which can rise far above the vehicle.
    const RenderWorldPoint player{worldCoordinate(camera.worldPosition[0]),worldCoordinate(camera.worldPosition[1]),worldCoordinate(camera.worldPosition[2])};
    stats.effectiveOpacity=style.opacity*std::clamp((distance(player,pose.position)-style.fadeNear)/(style.fadeFar-style.fadeNear),0.0,1.0);
    if(!stats.effectiveOpacity)return raster;
    carDepth_.assign(std::size_t(width)*height,0);
    carColor_.resize(carDepth_.size()); // Colour is read only where a fragment wrote depth.
    for(const auto& face:car.faces){
        std::array<RenderWorldPoint,4> points;for(unsigned n=0;n<4;++n)points[n]=car.vertices.at(face.vertices[n]);
        quad(points,camera,tables,width,height,padding,top,carDepth_,&carColor_,face.color,stats.triangles);
    }
    for(const auto& row:car.tyreRows){
        const auto left=renderCameraRelative(row[0],camera),right=renderCameraRelative(row[1],camera);
        // Clipped/behind-camera supports cannot define a safe projected axle.
        if(left.forward<nearPlane||right.forward<nearPlane)continue;
        const auto a=projectRenderWorldPoint(row[0],camera,tables),b=projectRenderWorldPoint(row[1],camera,tables);
        if(!a.inFront||!b.inFront)continue;
        const auto tyres=buildOpponentTyreRow({a.x,a.y},{b.x,b.y});
        for(unsigned n=0;n<2;++n){
            const auto fraction=n?0.875:0.125;
            const auto depth=1/(left.forward+(right.forward-left.forward)*fraction);
            std::array<Vertex,4> points;
            for(unsigned p=0;p<4;++p)points[p]={double(tyres[n][p].x+padding),double(tyres[n][p].y+top),depth};
            triangle(points[0],points[1],points[2],width,height,carDepth_,&carColor_,ghostTyreColor);
            triangle(points[0],points[2],points[3],width,height,carDepth_,&carColor_,ghostTyreColor);stats.triangles+=2;
        }
    }
    if(std::any_of(carDepth_.begin(),carDepth_.end(),[](float depth){return depth>0;}))raster.silhouette_=&carDepth_;
    return raster;
}
GhostCarStatistics GhostCarRenderer::composite(const Raster& raster,std::vector<std::uint32_t>& pixels,
    const std::vector<std::uint8_t>& coverage,const std::vector<float>* trackDepth,bool fullTrackDepth) const{
    const auto width=raster.width,height=raster.height;
    if(pixels.size()!=std::size_t(raster.canvasWidth*200)||coverage.size()!=std::size_t(width*height))
        throw std::invalid_argument("Ghost buffers do not match viewport");
    if(trackDepth&&trackDepth->size()!=coverage.size())throw std::invalid_argument("Painted depth does not match viewport");
    auto stats=raster.statistics;
    stats.checkedFullTrackDepth=trackDepth&&fullTrackDepth;
    if(!raster.silhouette()){
        if(stats.checkedFullTrackDepth)for(std::size_t n=0;n<coverage.size();++n)
            stats.trackPixelsWithoutDepth+=(coverage[n]==1||coverage[n]==2)&&(*trackDepth)[n]<=0;
        return stats;
    }
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto at=std::size_t(y*width+x);
        const auto carDepth=carDepth_[at];
        if(!carDepth&&!stats.checkedFullTrackDepth)continue;
        const bool track=trackDepth&&(coverage[at]==1||coverage[at]==2);
        const auto inverse=track?(*trackDepth)[at]:0;
        stats.trackPixelsWithoutDepth+=stats.checkedFullTrackDepth&&track&&!inverse;
        if(!carDepth)continue;
        stats.ghostPixelsWithoutTrackDepth+=track&&!inverse;
        ++stats.silhouettePixels;
        // Restrict the auxiliary mask to road/side pixels actually painted by
        // the original command stream. Never mask a ghost against an unseen
        // generic-mesh surface. Small depth tolerance handles raster rounding.
        // Unknown painted-track depth is conservative: hide that fragment
        // rather than letting it draw through an original fill boundary.
        if(track&&(!inverse||inverse>carDepth*1.002f+0.000001f)){++stats.occludedPixels;continue;}
        const auto pixel=std::size_t((y+raster.offsetY)*raster.canvasWidth+x+raster.offsetX);
        pixels[pixel]=blend(pixels[pixel],carColor_[at],stats.effectiveOpacity);++stats.visiblePixels;
    }
    return stats;
}
GhostCarStatistics GhostCarRenderer::draw(const GhostCarPose& pose,const GhostCarStyle& style,
    const TrackMesh& mesh,const std::vector<TrackSideFace>& sides,
    const RenderProjectionCamera& camera,const RenderProjectionTables& tables,
    std::vector<std::uint32_t>& pixels,const std::vector<std::uint8_t>& coverage,int padding,bool extended,int bottom,const std::vector<float>* paintedDepth){
    const auto raster=rasterize(pose,style,camera,tables,padding,extended,bottom);
    if(style.occlusion&&!paintedDepth&&(raster.silhouette()||style.auditTrackDepth)){
        worldDepth_.assign(std::size_t(raster.width)*raster.height,0);
        unsigned worldTriangles=0;
        const auto drawQuad=[&](const std::array<RenderWorldPoint,4>& points){
            quad(points,camera,tables,raster.width,raster.height,padding,extended?16:0,worldDepth_,nullptr,0,worldTriangles);
        };
        for(const auto& face:mesh.faces){
            std::array<RenderWorldPoint,4> points;
            for(unsigned n=0;n<4;++n){const auto& v=mesh.vertices.at(face.indices[n]);points[n]={v.x,v.y,v.z};}
            drawQuad(points);
        }
        for(const auto& face:sides){
            std::array<RenderWorldPoint,4> points;
            for(unsigned n=0;n<4;++n){const auto& v=face.vertices[n];points[n]={v.x,v.y,v.z};}
            drawQuad(points);
        }
        paintedDepth=&worldDepth_;
    }
    return composite(raster,pixels,coverage,style.occlusion?paintedDepth:nullptr);
}
} // namespace scr

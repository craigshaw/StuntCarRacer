#include "render/ghost_car.h"
#include "render/software_renderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
scr::RenderProjectionTables syntheticTables(){
    scr::RenderProjectionTables tables;
    constexpr double pi=3.14159265358979323846;
    for(unsigned n=0;n<2048;++n){const double r=double(n)/2048;
        tables.angleTable[n]=std::uint16_t(std::atan(r)*65536/(2*pi));
        tables.magnitudeTable[n]=std::uint16_t(n?(std::sqrt(1+r*r)-1)/r*65536:0);
    }
    return tables;
}
std::uint32_t blended(std::uint32_t a,std::uint32_t b,double t){
    std::uint32_t out=0;for(unsigned s:{0u,8u,16u})out|=std::uint32_t(std::lround(((a>>s)&255)*(1-t)+((b>>s)&255)*t))<<s;return out;
}
}
int main()try{
    using namespace scr;
    const auto tables=syntheticTables();
    RenderProjectionCamera camera;camera.worldPosition={512u<<16,47u<<16,512u<<16};
    camera.rotatedXUnits=camera.rotatedZUnits=512*16;camera.heightBaseline=47*32;
    camera.cosRoll=camera.cosYaw=0x7fff;
    const GhostCarPose pose{{512,40,560},0,0,0};
    const auto car=buildGhostCarMesh(pose);
    require(car.vertices.size()<=64&&car.faces.size()<=48,"Exterior exceeded its bounded model budget");
    double minX=1e9,maxX=-1e9;
    for(const auto& vertex:car.vertices){minX=std::min(minX,vertex.x);maxX=std::max(maxX,vertex.x);}
    require(std::abs(maxX-minX-ghostReferenceWidth/2)<1e-9&&
        std::abs(car.tyreRows[0][1].x-car.tyreRows[0][0].x-ghostReferenceWidth)<1e-9&&
        std::abs(car.tyreRows[0][0].z-car.tyreRows[1][0].z-ghostReferenceFootprintLength)<1e-9,
        "Exterior lost the calibrated body or tyre footprint");
    // The exterior must point along its recorded heading, with the low/thin
    // wedge nose at +Z. Reversing yaw to fix artwork would reverse its motion.
    double frontTop=pose.position.y,rearTop=pose.position.y;
    for(unsigned n=0;n<8;++n){const auto& p=car.vertices[n];
        if(p.z>pose.position.z)frontTop=std::max(frontTop,p.y);else rearTop=std::max(rearTop,p.y);}
    require(frontTop<rearTop,"Ghost wedge has its tall end at the front");
    auto pitched=pose;pitched.pitch=0x4000;
    const auto rotated=buildGhostCarMesh(pitched);
    const auto front=std::size_t(std::max_element(car.vertices.begin(),car.vertices.end(),[](const auto& a,const auto& b){return a.z<b.z;})-car.vertices.begin());
    const auto rear=std::size_t(std::min_element(car.vertices.begin(),car.vertices.end(),[](const auto& a,const auto& b){return a.z<b.z;})-car.vertices.begin());
    require(rotated.vertices[front].y>rotated.vertices[rear].y,"Positive pitch does not raise the front");
    auto rolled=pose;rolled.roll=0x4000;const auto roll=buildGhostCarMesh(rolled);
    const auto left=std::size_t(std::min_element(car.vertices.begin(),car.vertices.end(),[](const auto& a,const auto& b){return a.x<b.x;})-car.vertices.begin());
    const auto right=std::size_t(std::max_element(car.vertices.begin(),car.vertices.end(),[](const auto& a,const auto& b){return a.x<b.x;})-car.vertices.begin());
    require(roll.vertices[left].y>roll.vertices[right].y,"Positive roll does not raise the left side");
    // Compare the rendered axes in the game's half-height space, rather than
    // only checking their sign at an artificial quarter turn.
    for(unsigned angle:{0x0800u,0x1800u,0xe800u}){
        auto tilted=pose;tilted.pitch=std::uint16_t(angle);tilted.yaw=0x1555;
        auto m=buildGhostCarMesh(tilted);
        const auto dx=m.vertices[front].x-m.vertices[rear].x,dz=m.vertices[front].z-m.vertices[rear].z;
        const auto dy=(m.vertices[front].y-m.vertices[rear].y)/2;
        const auto expected=std::sin(angle*6.28318530717958647692/65536);
        require(std::abs(dy/ghostReferenceFootprintLength-expected)<1e-9,"Projection flattened the recorded pitch");
        require(std::abs(std::hypot(std::hypot(dx,dz),dy)-ghostReferenceFootprintLength)<1e-9,"Pitch distorted the model length");
        tilted.pitch=0;tilted.roll=std::uint16_t(angle);m=buildGhostCarMesh(tilted);
        require(std::abs((m.vertices[0].y-m.vertices[3].y)/ghostReferenceWidth-expected)<1e-9,"Projection flattened the recorded roll");
    }
    RenderCameraInputs raw;raw.worldPosition={512u<<16,0xffff8000u,560u<<16};
    require(makeGhostCarPose(raw).position.y==-0.5,"Wrapped negative car height was treated as an unsigned coordinate");
    const auto airborne=makeGhostCarPose(raw);raw.averageWheelVelocity=0x140;
    require(makeGhostCarPose(raw).suspensionLift==0&&airborne.suspensionLift==0,"Flat calibration or flight acquired a suspension lift");
    raw.averageWheelVelocity=0x440;const auto loaded=makeGhostCarPose(raw);
    require(loaded.position.y==-0.5&&loaded.suspensionLift==3,"Suspension load changed the physical trajectory or lost its exterior allowance");
    const auto loadedMesh=buildGhostCarMesh(loaded),airborneMesh=buildGhostCarMesh(airborne);
    for(unsigned n=0;n<loadedMesh.vertices.size();++n)require(std::abs(loadedMesh.vertices[n].y-airborneMesh.vertices[n].y-3)<1e-9,"Suspension allowance was rotated with the body");
    for(unsigned row=0;row<2;++row)for(unsigned side=0;side<2;++side)
        require(std::abs(loadedMesh.tyreRows[row][side].y-airborneMesh.tyreRows[row][side].y-3)<1e-9,"Tyre supports lost the recorded suspension allowance");
    raw.averageWheelVelocity=0xffff;require(makeGhostCarPose(raw).suspensionLift==0,"Negative response raised the exterior");
    const std::uint32_t background=0x203040;
    std::vector<std::uint32_t> pixels(320*200,background);
    std::vector<std::uint8_t> coverage(256*128,0);
    GhostCarRenderer renderer;GhostCarStyle style;
    const auto visible=renderer.draw(pose,style,{}, {},camera,tables,pixels,coverage);
    require(visible.visiblePixels>20&&visible.silhouettePixels==visible.visiblePixels,"Free ghost was not visible");
    std::set<std::uint32_t> once;
    for(const auto& face:car.faces)once.insert(blended(background,face.color,style.opacity));
    once.insert(blended(background,ghostTyreColor,style.opacity));
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){const auto p=pixels[y*320+x];
        if(p!=background){require(x>=32&&x<288&&y>=16&&y<144,"Ghost escaped the Original viewport");require(once.count(p)!=0,"Self-overlapping faces were blended more than once");}
    }
    const auto full=pixels;
    const auto outside=makeNearTrackEdge({-100,10,0.1},{-40,90,0.02});
    for(const auto* point:{&outside.clippedFrom,&outside.clippedTo})
        require(point->inverseDepth>=0.02-1e-9&&point->inverseDepth<=0.1+1e-9,
            "Viewport closure extrapolated depth beyond a rejected source edge");
    // Depth uses the very same clipped painter spans as colour, including the
    // fact that a later painter command wins even if its depth is farther away.
    const auto rectangle=[](int x0,int y0,int x1,int y1,double inverse,NearTrackSurface surface){
        NearTrackDrawCommand command;command.kind=NearTrackCommandKind::fill;command.surface=surface;command.color=1;
        const std::array<NearTrackPoint,4> points{{{x0,y0,inverse},{x1,y0,inverse},{x1,y1,inverse},{x0,y1,inverse}}};
        for(unsigned n=0;n<4;++n)command.edges.push_back(makeNearTrackEdge(points[n],points[(n+1)%4]));
        return command;
    };
    NearTrackPresentation painted;painted.coveredSegments.fill(true);
    painted.commands={rectangle(-80,-30,340,160,1.0/90,NearTrackSurface::road),
        rectangle(-40,50,128,160,1.0/20,NearTrackSurface::rightSide),
        rectangle(50,75,90,120,1.0/180,NearTrackSurface::road)};
    std::array<std::uint32_t,16> palette{};palette.fill(background);palette[1]=0x403020;
    SoftwareRenderer painter;std::vector<std::uint32_t> painterPixels(320*200,background),withoutDepth=painterPixels;
    painter.drawFrame({}, {}, {},camera,tables,palette,withoutDepth,false,&painted);
    painter.drawFrame({}, {}, {},camera,tables,palette,painterPixels,false,&painted,0,false,0,true);
    require(painterPixels==withoutDepth,"Capturing painter depth changed original pixels");
    const auto& depth=painter.worldInverseDepth();
    require(std::none_of(depth.begin(),depth.end(),[](float d){return d<=0;}),"Clipped painter span has a depth coverage hole");
    require(std::abs(depth[90*256+60]-1.0/180)<1e-7,"Depth was sorted instead of following the painter");
    const auto paintedGhost=renderer.draw(pose,style,{}, {},camera,tables,painterPixels,painter.worldCoverage(),0,false,0,&depth);
    require(paintedGhost.visiblePixels>0&&paintedGhost.occludedPixels>0&&!paintedGhost.ghostPixelsWithoutTrackDepth,
        "Painted road/side depth did not occlude the ghost without gaps");
    const auto physicalFill=[&](const std::array<RenderWorldPoint,4>& points,NearTrackSurface surface){
        auto command=rectangle(0,0,1,1,1,surface);command.edges.clear();std::array<NearTrackPoint,4> projected;
        std::array<RenderCameraRelative,4> sources;
        for(unsigned n=0;n<4;++n){const auto p=projectRenderWorldPoint(points[n],camera,tables);sources[n]=renderCameraRelative(points[n],camera);
            projected[n]={p.x,p.y,1/sources[n].forward,sources[n]};}
        for(unsigned n=0;n<4;++n)command.edges.push_back(makeNearTrackEdge(projected[n],projected[(n+1)%4]));
        command.depthPatches.push_back(sources);return command;
    };
    painted.commands={physicalFill({{{488,40,528},{536,40,528},{536,40,640},{488,40,640}}},NearTrackSurface::road)};
    painterPixels.assign(painterPixels.size(),background);withoutDepth=painterPixels;
    painter.drawFrame({}, {}, {},camera,tables,palette,withoutDepth,false,&painted);
    painter.drawFrame({}, {}, {},camera,tables,palette,painterPixels,false,&painted,0,false,0,true);
    require(painterPixels==withoutDepth,"Angular source patches changed painted colour pixels");
    for(unsigned y:{80u,96u,100u}){
        constexpr double radians=6.28318530717958647692/65536;
        const auto side=(128.5-128)/(32767.0/32768)*32*radians;
        const auto down=(y+.5-64)/(32767.0/32768)*32*radians;
        const auto inverse=std::tan(down)/std::cos(side)/3.5;
        require(std::abs(painter.worldInverseDepth()[y*256+128]-inverse)<1e-7,("Angular road depth mismatch at row "+std::to_string(y)+": "+std::to_string(painter.worldInverseDepth()[y*256+128])+" versus "+std::to_string(inverse)).c_str());
    }
    painted.commands={physicalFill({{{480,10,536},{544,10,536},{544,80,536},{480,80,536}}},NearTrackSurface::rightSide)};
    painterPixels.assign(painterPixels.size(),background);
    painter.drawFrame({}, {}, {},camera,tables,palette,painterPixels,false,&painted,0,false,0,true);
    require(renderer.draw(pose,style,{}, {},camera,tables,painterPixels,painter.worldCoverage(),0,false,0,&painter.worldInverseDepth()).visiblePixels==0,
        "Source-patch depth let the ghost draw through a foreground wall");
    TrackMesh wall;wall.vertices={{480,10,536},{544,10,536},{544,80,536},{480,80,536}};
    wall.faces.push_back({{0,1,2,3},0,0});coverage.assign(coverage.size(),1);pixels.assign(pixels.size(),background);
    const auto hidden=renderer.draw(pose,style,wall,{},camera,tables,pixels,coverage);
    require(hidden.visiblePixels==0&&hidden.occludedPixels==visible.silhouettePixels,"Opaque foreground did not hide the complete ghost");
    TrackSideFace side;for(unsigned n=0;n<4;++n)side.vertices[n]=wall.vertices[n];
    coverage.assign(coverage.size(),2);pixels.assign(pixels.size(),background);
    require(renderer.draw(pose,style,{}, {side},camera,tables,pixels,coverage).visiblePixels==0,
        "Foreground track sidewall did not hide the ghost");
    wall.vertices[1].x=wall.vertices[2].x=512;pixels.assign(pixels.size(),background);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x)coverage[y*256+x]=x<128?1:0;
    const auto partial=renderer.draw(pose,style,wall,{},camera,tables,pixels,coverage);
    require(partial.visiblePixels>0&&partial.occludedPixels>0,"Partial wall did not cut the ghost silhouette");
    // A mesh surface absent from the recovered painted world cannot occlude.
    coverage.assign(coverage.size(),0);pixels.assign(pixels.size(),background);
    renderer.draw(pose,style,wall,{},camera,tables,pixels,coverage);
    require(pixels==full,"Unpainted auxiliary geometry hid the ghost");
    coverage.assign(coverage.size(),1);pixels.assign(pixels.size(),background);
    const auto unknown=renderer.draw(pose,style,{}, {},camera,tables,pixels,coverage);
    require(!unknown.visiblePixels&&unknown.ghostPixelsWithoutTrackDepth==unknown.silhouettePixels,
        "Unknown depth under an original track fill exposed the ghost");
    coverage.assign(coverage.size(),0);
    style.opacity=0;pixels.assign(pixels.size(),background);
    require(renderer.draw(pose,style,{}, {},camera,tables,pixels,coverage).visiblePixels==0&&
        std::count(pixels.begin(),pixels.end(),background)==320*200,"Disabled ghost changed pixels");
    style={};auto close=pose;close.position={512,47,512};
    require(renderer.draw(close,style,{}, {},camera,tables,pixels,coverage).effectiveOpacity==0,"Overlapping ghost did not fade completely");
    auto raisedCamera=camera;raisedCamera.heightBaseline=200*32;
    require(renderer.draw(close,style,{}, {},raisedCamera,tables,pixels,coverage).effectiveOpacity==0,
        "Speed-dependent camera height defeated overlap fading");
    style.fadeNear=0;style.fadeFar=1;close.position.z+=2;
    renderer.draw(close,style,{}, {},camera,tables,pixels,coverage); // Near-plane intersection must be bounded.
    auto behind=pose;behind.position.z=480;
    require(renderer.draw(behind,style,{}, {},camera,tables,pixels,coverage).silhouettePixels==0,"Car behind camera wrapped into view");
    unsigned views=0;
    for(int padding:{32,88,304}){const int bottom=padding==32?0:56;
        pixels.assign((256+2*padding)*200,background);coverage.assign((256+2*padding)*(144+bottom),0);
        require(renderer.draw(pose,style,{}, {},camera,tables,pixels,coverage,padding,true,bottom).visiblePixels>0,"Expanded ghost view is empty");++views;
    }
    // Follow a car outside the original 4:3 horizontal aperture on either
    // side. Widescreen must retain it rather than borrowing original bounds.
    for(double offset:{-30.0,30.0}){auto beside=pose;beside.position.x+=offset;
        pixels.assign(320*200,background);coverage.assign(256*128,0);
        require(!renderer.draw(beside,style,{}, {},camera,tables,pixels,coverage).silhouettePixels,"Aperture fixture remained inside Original");
        for(int padding:{88,304}){pixels.assign((256+2*padding)*200,background);coverage.assign((256+2*padding)*200,0);
            require(renderer.draw(beside,style,{}, {},camera,tables,pixels,coverage,padding,true,56).visiblePixels>0,"Wide ghost was clipped to original screen bounds");}
    }
    bool rejected=false;style.opacity=std::numeric_limits<double>::quiet_NaN();
    try{renderer.draw(pose,style,{}, {},camera,tables,pixels,coverage,304,true,56);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Nonfinite ghost opacity was accepted");
    std::cout<<"{\"status\":\"passed\",\"free_pixels\":"<<visible.visiblePixels<<",\"partial_hidden_pixels\":"<<partial.occludedPixels
        <<",\"expanded_views\":"<<views<<",\"single_blend\":true,\"near_clipping\":true,\"negative_height\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

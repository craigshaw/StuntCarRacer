#include "presentation/ghost_bridge.h"
#include "presentation/camera_view.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main()try{
    using namespace scr;using namespace scr::runtime;
    TrackGeometryData track;track.geometry.resize(256);TrackMeshData mesh;
    TrackMeshSection section;section.segment=0x33;section.points[0][2]=1200;section.points[1][2]=1300;mesh.sections.push_back(section);
    const auto shape=captureGhostBridgeShape(track,mesh,32);
    require(shape==captureGhostBridgeShape(track,mesh,32),"Equal bridge shape differs");
    auto changed=track;changed.geometry[42]=1;
    require(!(shape==captureGhostBridgeShape(changed,mesh,32)),"Near geometry mismatch was ignored");
    mesh.sections[0].points[0][2]++;
    require(!(shape==captureGhostBridgeShape(track,mesh,32)),"Cached geometry mismatch was ignored");
    bool rejected=false;try{captureGhostBridgeShape(track,mesh,250);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Invalid bridge range was accepted");
    GhostBridgeRegionTracker region;
    require(region.observe(0x30,true)==GhostBridgeRegion::outside,"Suppression extends beyond approach");
    require(region.observe(0x31,true)==GhostBridgeRegion::approach,"Missing bridge approach");
    require(region.observe(0x33,false)==GhostBridgeRegion::crossing,"Missing bridge crossing");
    require(region.observe(0x38,false)==GhostBridgeRegion::landing,"Airborne ghost left guard before landing");
    require(region.observe(0x39,true)==GhostBridgeRegion::landing,"One ground observation ended guard");
    require(region.observe(0x39,true)==GhostBridgeRegion::outside,"Grounded landing did not clear guard");
    region.observe(0x33,false);
    require(region.observe(0x20,false,true)==GhostBridgeRegion::outside,"Recovery cut retained bridge guard");
    GhostBridgeVisibility visibility;
    visibility.observe(0,GhostBridgeRegion::outside,false);
    require(visibility.alphaAt(0.5)==1,"Remote bridge mismatch hid the ghost");
    visibility.observe(1,GhostBridgeRegion::approach,false);
    require(std::abs(visibility.alphaAt(1.12)-0.5)<1e-8,"Bridge fade did not use simulation time");
    const auto paused=visibility.alphaAt(1.12);
    for(unsigned n=0;n<20;++n)require(visibility.alphaAt(1.12)==paused,"Repeated/paused query advanced fade");
    visibility.observe(1.24,GhostBridgeRegion::crossing,true);
    require(visibility.alphaAt(1.4)==0&&visibility.conditionsDifferAt(1.4),"Transient equal shape caused crossing flicker");
    visibility.observe(1.48,GhostBridgeRegion::landing,true);
    require(visibility.alphaAt(1.6)==0,"Ghost returned before landing cleared");
    visibility.observe(2,GhostBridgeRegion::outside,false);
    require(std::abs(visibility.alphaAt(2.12)-0.5)<1e-8&&!visibility.conditionsDifferAt(2.12),"Ghost did not fade back after safe exit");
    require(visibility.alphaAt(1.12)==paused&&visibility.alphaAt(2.5)==1,"Reverse query changed fade history");
    visibility.observe(3,GhostBridgeRegion::crossing,false,true);
    require(visibility.alphaAt(3)==0,"Recovery cut blended across incompatible geometry");
    rejected=false;try{visibility.observe(2,GhostBridgeRegion::outside,true);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Out-of-order live observation was accepted");
    PresentationPose source;source.pose.worldPosition={512u<<16,40u<<16,512u<<16};
    CameraViewSettings camera;camera.worldOffset={-32,12,0};camera.showCockpit=false;TrigTable trig{};
    const auto observer=makeCameraView(source,camera,trig);
    require(observer.pose.worldPosition[0]==480u<<16&&observer.pose.worldPosition[1]==52u<<16&&source.pose.worldPosition[0]==512u<<16,
        "Observer camera changed the driver or used wrong world units");
    camera.worldOffset[0]=std::numeric_limits<double>::infinity();rejected=false;
    try{makeCameraView(source,camera,trig);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Nonfinite observer displacement was accepted");
    std::cout<<"{\"status\":\"passed\",\"near_and_cache_compared\":true,\"landing_guard\":true,\"pure_fade_queries\":true,\"observer_isolated\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

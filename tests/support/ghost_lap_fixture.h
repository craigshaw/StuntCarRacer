#pragma once
#include "race/lap_recording.h"

namespace scr::test::ghost {
inline runtime::GhostLap lap(const std::string& career,unsigned track=0,bool super=false){
    runtime::GhostLap result;result.context={{career,track,super},runtime::GhostLapSource::Practice,1234};
    result.completedTime={0,1,0x23};result.hundredths=123;result.controlPresets=3;result.durationMicroseconds=240000;
    result.start.progress=255;result.finish={240000,240000,255,0,false,false};
    for(unsigned n=0;n<3;++n){
        runtime::GhostLapSample s;s.microseconds=n*120000;s.pose.worldPosition={300u<<16,0xffff0000u,(100+n*8)<<16};
        s.pose.yaw=std::uint16_t(65535-n);s.pose.pitch=0x8000;s.pose.roll=0x7fff;s.pose.averageWheelVelocity=0x180;s.cut=n==1;
        s.segment=std::uint8_t(track==5?51:0);
        if(track==5){s.bridge=0;s.region=runtime::GhostBridgeRegion::crossing;}result.samples.push_back(s);
    }
    if(track==5){runtime::GhostBridgeShape shape;shape.nearGeometry[0]=7;shape.cachedSections={0x233,900,901};result.bridges.push_back(shape);}
    return result;
}
} // namespace scr::test::ghost

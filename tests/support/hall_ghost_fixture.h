#pragma once
#include "hall_fixture.h"
#include "persistence/hall_ghost_codec.h"

namespace scr::test::hall {
inline runtime::HallGhost ghost(const runtime::HallAttempt& a){
    runtime::HallGhost g;g.catalog=catalog;g.attempt=a.id;g.board=a.board;g.time=a.time;
    g.durationMicroseconds=240000;g.start.progress=255;g.finish={240000,240000,255,0,false,false};
    for(unsigned n=0;n<3;++n){runtime::GhostLapSample s;s.microseconds=n*120000;s.pose.worldPosition={1,2,n*100};
        s.pose.pitch=std::uint16_t(n*500);s.pose.yaw=65535;s.pose.roll=0x8000;s.pose.averageWheelVelocity=77;s.cut=n==1;
        if(a.board.track==5){s.bridge=0;s.region=runtime::GhostBridgeRegion::crossing;s.segment=51;}g.samples.push_back(s);}
    if(a.board.track==5){runtime::GhostBridgeShape b;b.cachedSections={0x233,900,901};g.bridges.push_back(b);}return g;
}
} // namespace scr::test::hall

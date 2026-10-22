#pragma once
#include "near_track_visibility.h"
#include "near_track_geometry_fixture.h"
namespace scr::test::nearVisibility {
using nearGeometry::Ram;
constexpr unsigned queueBase=0x7b08a,lineBase=0x3d80,lineEnd=0x60a8;
using timing::word;using timing::longword;using timing::putWord;using timing::putLong;
inline void writeQueueInputs(Ram& r,const NearTrackQueueState& q){
    putWord(r.data(),0x68148,q.offset);putLong(r.data(),0x68144,lineBase+q.lineBytes);
    r[0x1bba6]=q.opponentSegment;r[0x1bc24]=q.opponentSubSegment;r[0x1bb7c]=q.previousOffset;
    putWord(r.data(),0x1bc12,q.opponentOffset);putWord(r.data(),0x63ede,q.minimumBoundaryDistance);
    for(unsigned n=0;n<q.entries.size();++n){for(unsigned e=0;e<7;++e)putLong(r.data(),queueBase+n*32+e*4,0x80000000);
        const auto& v=q.entries[n];r[queueBase+n*32+28]=v.segment;r[queueBase+n*32+29]=v.edgeColor;r[queueBase+n*32+30]=v.processedFlags;r[queueBase+n*32+31]=v.steeringFlags;}
    r[0x1bb77]=0;
}
} // namespace scr::test::nearVisibility

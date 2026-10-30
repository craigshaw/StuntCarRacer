#pragma once
#include "render/track_mesh.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace scr::state::mesh {
constexpr unsigned trackMeshStreamBase=0x7abda,trackMeshPointerTable=0x7aa1a,trackMeshEndPointer=0x66102;
// Decode the recovered state layout for native mesh algorithms. The same
// adapter accepts fresh native state and captured verification state.
inline TrackMeshData decodeTrackMesh(const std::array<std::uint8_t,0x80000>& r) {
    const auto word=[&](unsigned a){return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);};
    const auto longword=[&](unsigned a){return (std::uint32_t(word(a))<<16)|word(a+2);};
    const auto end=longword(trackMeshEndPointer);
    if(end<=trackMeshStreamBase||end>r.size()||(end-trackMeshStreamBase)%14)
        throw std::invalid_argument("Original generated track stream has invalid bounds");
    TrackMeshData d;d.segmentCount=r[0x1ca1a];
    if(!d.segmentCount||d.segmentCount>100)throw std::invalid_argument("Mesh capture has no loaded track");
    std::copy_n(r.begin()+0x1c588,100,d.segmentCoordinates.begin());
    for(unsigned a=trackMeshStreamBase;a<end;a+=14) {
        TrackMeshSection section{r[a+1],r[a],{}};
        for(unsigned side=0;side<2;++side)for(unsigned component=0;component<3;++component)
            section.points[side][component]=word(a+2+6*side+2*component);
        if(section.segment>=d.segmentCount)throw std::invalid_argument("Original mesh section references an unloaded segment");
        d.sections.push_back(section);
    }
    for(unsigned segment=0;segment<d.segmentCount;++segment) {
        const auto pointer=longword(trackMeshPointerTable+4*segment);
        const auto next=segment+1<d.segmentCount?longword(trackMeshPointerTable+4*(segment+1)):end;
        // Some original segments emit no cached cross-section: their pointer
        // aliases the next segment, or the stream end. Validate the interval
        // and require ownership only when that interval contains geometry.
        if(pointer<trackMeshStreamBase||pointer>end||next<pointer||next>end||
           (pointer-trackMeshStreamBase)%14||(next-trackMeshStreamBase)%14||
           (segment==0&&pointer!=trackMeshStreamBase))
            throw std::invalid_argument("Original track pointer table does not select its segment");
        for(auto at=pointer;at<next;at+=14)if(r[at+1]!=segment)
            throw std::invalid_argument("Track cache interval has another segment's geometry");
    }
    return d;
}
} // namespace scr::state::mesh

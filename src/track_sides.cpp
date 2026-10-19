#include "track_sides.h"
#include <stdexcept>

namespace scr {
namespace {int signedWord(std::uint16_t v) noexcept {return v<0x8000?int(v):int(v)-65536;}}
std::uint8_t trackRoadFillColor(std::uint8_t segment,std::uint8_t flags) noexcept {
    return flags&0x20?0:std::uint8_t(segment&1?2:1);
}
std::uint8_t trackSideFillColor(std::uint8_t segment,const TrackSideStyle& style,bool near) noexcept {
    if(near&&style.nearColorOverride)return style.edgeColor;
    return segment&1?style.alternateFillColor:15;
}
bool trackSurfaceCrossEdge(std::uint8_t flags) noexcept {return (flags&1)!=0;}
std::uint8_t trackRoadEdgeColor(std::uint8_t depth,bool alternate) noexcept {
    const int signedDepth=depth<128?int(depth):int(depth)-256;
    const auto base=std::uint8_t(signedDepth<38?9:signedDepth<42?10:11);
    return signedDepth>=44?std::uint8_t(base|0x80):alternate?3:base;
}
bool trackSideVertexRequired(TrackSide side,std::uint16_t current,std::uint16_t previous,
    std::uint8_t offRoad,std::uint8_t flags) noexcept {
    const int x=signedWord(current),old=signedWord(previous);
    if(x==old&&offRoad)return true;
    return side==TrackSide::right?(x<old||(flags&0x80)):(x>old||(flags&0x40));
}
std::vector<TrackSideFace> buildTrackSides(const TrackMesh& mesh,const TrackSideStyle& style) {
    if(style.edgeColor>15||style.alternateFillColor>15)throw std::invalid_argument("Track side palette index exceeds15");
    std::vector<TrackSideFace> result;result.reserve(mesh.faces.size()*2);
    for(const auto& face:mesh.faces)for(unsigned side=0;side<2;++side){
        const auto first=face.indices[side?3:0],second=face.indices[side?2:1];
        if(first>=mesh.vertices.size()||second>=mesh.vertices.size())throw std::out_of_range("Track side road index outside mesh");
        const auto& a=mesh.vertices[first];const auto& b=mesh.vertices[second];
        result.push_back({{{a,b,{b.x,trackSideGroundHeight,b.z},{a.x,trackSideGroundHeight,a.z}}},
            face.segment,face.flags,trackSideFillColor(face.segment,style),style.edgeColor,side?TrackSide::left:TrackSide::right});
    }
    return result;
}
} // namespace scr

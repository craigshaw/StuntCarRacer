#include "render/track_mesh.h"
#include <stdexcept>

namespace scr {
namespace {
double signedWord(std::uint16_t x) noexcept {return x<0x8000?double(x):double(int(x)-65536);}
}
TrackMesh buildTrackMesh(const TrackMeshData& data) {
    if(!data.segmentCount||data.segmentCount>100||data.sections.size()<2)
        throw std::invalid_argument("Track mesh requires loaded segments and at least two cross-sections");
    TrackMesh mesh;mesh.vertices.reserve(data.sections.size()*2);mesh.faces.reserve(data.sections.size());
    for(const auto& section:data.sections) {
        if(section.segment>=data.segmentCount)throw std::out_of_range("Track mesh section references an unloaded segment");
        const auto cell=data.segmentCoordinates[section.segment];
        for(const auto& point:section.points)mesh.vertices.push_back({(cell&15)*128.0+signedWord(point[0])/16.0,
            signedWord(point[2])/32.0,(cell>>4)*128.0+signedWord(point[1])/16.0});
    }
    for(unsigned i=0;i<data.sections.size();++i) {
        // Original drawTrackLines joins previous D2 to current D1, then reads
        // the current header for the polygon. Cached sections omit the start
        // vertex of each segment, so the next section owns this strip.
        const auto next=(i+1)%unsigned(data.sections.size());const auto& section=data.sections[next];
        mesh.faces.push_back({{2*i,2*next,2*next+1,2*i+1},section.segment,section.flags});
    }
    return mesh;
}
} // namespace scr

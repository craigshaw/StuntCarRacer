#pragma once
#include "render_projection.h"
#include "track_mesh.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
struct RenderStatistics {
    unsigned roadFaces{},clippedFaces{},triangles{},roadPixels{};
};
// First native presentation backend. Geometry and angular point projection
// come from the recovered game; polygon clipping, depth buffering and triangle
// coverage are explicit viewer choices, not claims of original pixel identity.
// Pixels are packed 0x00RRGGBB, suitable for a Windows BGRX DIB.
class SoftwareReplayRenderer {
public:
    static constexpr int width=320,height=200;
    static constexpr int viewportX=32,viewportY=16,viewportWidth=256,viewportHeight=128;
    RenderStatistics drawTrack(const TrackMesh&,const RenderProjectionCamera&,
        const RenderProjectionTables&,const std::array<std::uint32_t,16>& palette,
        std::vector<std::uint32_t>& pixels,bool wireframe=false);
private:
    std::vector<float> inverseDepth_;
};
} // namespace scr

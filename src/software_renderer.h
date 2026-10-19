#pragma once
#include "render_projection.h"
#include "track_mesh.h"
#include "track_sides.h"
#include "mountain_horizon.h"
#include "near_track_frame.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
struct RenderStatistics {
    unsigned roadFaces{},sideFaces{},mountainInstances{},mountainPolygons{};
    unsigned clippedFaces{},triangles{},roadPixels{},sidePixels{},mountainPixels{};
    unsigned nearQueueEntries{},nearFillCommands{},edgeHighlightCommands{},edgeHighlightPixels{};
};
// First native presentation backend. Geometry and angular point projection
// come from the recovered game; polygon clipping, depth buffering and triangle
// coverage are explicit viewer choices, not claims of original pixel identity.
// Pixels are packed 0x00RRGGBB, suitable for a Windows BGRX DIB.
class SoftwareReplayRenderer {
public:
    static constexpr int width=320,height=200;
    static constexpr int viewportX=32,viewportY=16,viewportWidth=256,viewportHeight=128;
    RenderStatistics drawFrame(const TrackMesh&,const std::vector<TrackSideFace>&,
        const MountainHorizonFrame&,const RenderProjectionCamera&,
        const RenderProjectionTables&,const std::array<std::uint32_t,16>& palette,
        std::vector<std::uint32_t>& pixels,bool wireframe=false,const NearTrackPresentation* nearTrack=nullptr);
private:
    std::vector<float> inverseDepth_;
    std::vector<std::uint8_t> coverage_;
    std::vector<std::uint8_t> highlights_;
};
} // namespace scr

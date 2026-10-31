#pragma once
#include "render/render_projection.h"
#include "render/track_mesh.h"
#include "render/track_sides.h"
#include "render/mountain_horizon.h"
#include "render/near_track_frame.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
struct RenderStatistics {
    unsigned roadFaces{},sideFaces{},mountainInstances{},mountainPolygons{};
    unsigned clippedFaces{},triangles{},roadPixels{},sidePixels{},mountainPixels{};
    unsigned nearQueueEntries{},nearFillCommands{},edgeHighlightCommands{},edgeHighlightPixels{};
    unsigned opponentPolygons{},opponentPixels{};
};
// Native presentation backend for recovered, ordered scene commands. The
// generic mesh path remains available for isolated diagnostics. Line and
// polygon coverage are native choices, not claims of original pixel identity.
// Pixels are packed 0x00RRGGBB, suitable for a Windows BGRX DIB.
class SoftwareRenderer {
public:
    static constexpr int width=320,height=200;
    static constexpr int viewportX=32,viewportY=16,viewportWidth=256,viewportHeight=128;
    RenderStatistics drawFrame(const TrackMesh&,const std::vector<TrackSideFace>&,
        const MountainHorizonFrame&,const RenderProjectionCamera&,
        const RenderProjectionTables&,const std::array<std::uint32_t,16>& palette,
        std::vector<std::uint32_t>& pixels,bool wireframe=false,const NearTrackPresentation* nearTrack=nullptr,int horizontalPadding=0,bool extendedCorners=false,int bottomPadding=0,bool captureTrackDepth=false);
    // Read-only classification of the last painted viewport: road=1, side=2,
    // mountain=3, opponent=4. Auxiliary presentation must not alter this layer.
    const std::vector<std::uint8_t>& worldCoverage()const noexcept{return coverage_;}
    const std::vector<float>& worldInverseDepth()const noexcept{return inverseDepth_;}
private:
    std::vector<float> inverseDepth_;
    std::vector<std::uint8_t> coverage_;
    std::vector<std::uint8_t> highlights_;
};
} // namespace scr

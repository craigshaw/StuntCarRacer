#pragma once
#include "render/render_projection.h"
#include "render/track_mesh.h"
#include "render/track_sides.h"
#include <cstdint>
#include <vector>

namespace scr {
// Presentation-only exterior model for the static-track ghost proof. Position
// uses mesh/high-word world units, not the speed-dependent cockpit camera height.
struct GhostCarPose {
    RenderWorldPoint position;
    std::uint16_t pitch{},yaw{},roll{};
};
GhostCarPose makeGhostCarPose(const RenderCameraInputs&) noexcept;
struct GhostCarFace {
    std::array<unsigned,4> vertices{};
    std::uint32_t color{};
};
struct GhostCarMesh {
    std::vector<RenderWorldPoint> vertices;
    std::vector<GhostCarFace> faces;
};
// Visual calibration against a flat recovered opponent footprint. This is a
// native exterior model, not a recovered player-car mesh or collision volume.
constexpr double ghostReferenceWidth=5.0625,ghostReferenceFootprintLength=7.5625;
GhostCarMesh buildGhostCarMesh(const GhostCarPose&);

struct GhostCarStyle {
    double opacity{0.36};
    bool occlusion{true}; // Disabled only for proof comparison images.
    double fadeNear{10},fadeFar{24};
};
struct GhostCarStatistics {
    unsigned silhouettePixels{},visiblePixels{},occludedPixels{},triangles{};
    unsigned trackPixelsWithoutDepth{};
    unsigned ghostPixelsWithoutTrackDepth{};
    double effectiveOpacity{};
};
// Resolve the car once, then test against depth from the original painter.
// Standalone diagnostic meshes can omit that layer and use auxiliary geometry.
// World pixels, command ordering and recovered line allocation are not scratch.
class GhostCarRenderer {
public:
    GhostCarStatistics draw(const GhostCarPose&,const GhostCarStyle&,
        const TrackMesh&,const std::vector<TrackSideFace>&,
        const RenderProjectionCamera&,const RenderProjectionTables&,
        std::vector<std::uint32_t>& pixels,const std::vector<std::uint8_t>& worldCoverage,
        int horizontalPadding=0,bool extended=false,int bottomPadding=0,
        const std::vector<float>* paintedDepth=nullptr);
private:
    std::vector<float> worldDepth_,carDepth_;
    std::vector<std::uint32_t> carColor_;
};
} // namespace scr

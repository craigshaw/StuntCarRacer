#pragma once
#include "track_mesh.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
// The original calls these barriers, but the lower vertices are at raw road
// height0x0200 (world Y16). They are the sides of the raised road, not rails.
constexpr double trackSideGroundHeight=16.0;
enum class TrackSide : std::uint8_t { right,left };
struct TrackSideStyle {
    std::uint8_t edgeColor{},alternateFillColor{};
    bool nearColorOverride{}; // original segmentRenderingFlag
};
struct TrackSideFace {
    std::array<TrackMeshVertex,4> vertices{}; // road0,road1,ground1,ground0
    std::uint8_t segment{},flags{},fillColor{},edgeColor{};
    TrackSide side{};
};
// Same immutable strip as buildTrackMesh. The original renderer chooses visible
// edges before filling; the native viewer may use depth testing of these faces.
[[nodiscard]] std::vector<TrackSideFace> buildTrackSides(const TrackMesh& mesh,const TrackSideStyle& style);
// Far queue's +0x1c is segment index; +0x1e is generated section flags.
[[nodiscard]] std::uint8_t trackRoadFillColor(std::uint8_t segment,std::uint8_t flags) noexcept;
[[nodiscard]] std::uint8_t trackSideFillColor(std::uint8_t segment,const TrackSideStyle& style,bool near=false) noexcept;
[[nodiscard]] bool trackSurfaceCrossEdge(std::uint8_t flags) noexcept;
// Near queue's longitudinal-edge byte; bit7 means no edge. Alternation is
// ((segmentAlternateFlag<<1)^crossSectionByteOffset)&4 in its producer.
[[nodiscard]] std::uint8_t trackRoadEdgeColor(std::uint8_t segmentDepth,bool alternate) noexcept;
// Exact lower-vertex admission in original transformCoordinates, before screen
// roll. Angular X words are signed; flags are the generated header HIGH byte.
[[nodiscard]] bool trackSideVertexRequired(TrackSide side,std::uint16_t currentAngularX,
    std::uint16_t previousAngularX,std::uint8_t offRoadSideFlags,std::uint8_t flags) noexcept;
} // namespace scr

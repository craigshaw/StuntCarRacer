#pragma once
#include "render_projection.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scr {
struct MountainPoint { int x{},y{}; };
struct MountainFace {
    std::uint8_t color{},vertexCount{};
    // Original line-command indices and the closed boundary recovered from
    // those lines. Three or four edges form each admitted silhouette face.
    std::array<std::uint8_t,4> edges{},vertices{};
};
struct MountainShape {
    std::vector<MountainPoint> vertices;
    std::vector<std::array<std::uint8_t,2>> edges;
    std::vector<MountainFace> faces;
    std::uint32_t templateAddress{},variationAddress{};
};
struct MountainPlacement { std::uint8_t horizontalAngle{},shapeIndex{}; };
struct MountainHorizonData {
    std::array<MountainShape,25> shapes;
    std::vector<MountainPlacement> placements;
    std::uint32_t placementAddress{};
};
struct MountainInstance {
    std::uint8_t placementIndex{},shapeIndex{};
    std::vector<MountainPoint> vertices;
};
struct MountainScreenPolygon {
    std::array<MountainPoint,4> vertices{};
    std::uint8_t vertexCount{},color{},placementIndex{},shapeIndex{};
};
struct MountainHorizonFrame {
    // Original descending placement order, then the template's face order.
    std::vector<MountainInstance> instances;
    std::vector<MountainScreenPolygon> polygons;
};
// Decode the original table at0x699b8, five templates and their substitutions
// from the verified, locally extracted payload (load address0xe700). No game
// image bytes are compiled into the program. The disk loader selects table0
// after an unused track-ID read; both disk table entries share the same list.
MountainHorizonData decodeMountainHorizonData(const std::uint8_t* payload,std::size_t size);
// Exact selector and signed-word coordinate pipeline from0x6953e, followed by
// the original Q15 screen roll. yaw is the evolved full cameraAngleY word,
// rather than the quadrant-reduced road camera yawResidual. Points are local
// to the256x128 viewport; rasterization/clipping belongs to the caller.
MountainHorizonFrame projectMountainHorizon(const MountainHorizonData&,
    std::uint16_t yaw,const RenderProjectionCamera&,int horizontalPadding=0);
} // namespace scr

#pragma once
#include "physics/transform_matrices.h"
#include <array>
#include <cstdint>

namespace scr {
struct RenderProjectionTables {
    // Original tables0x1cc46 and0x1dc46, decoded from big-endian words.
    std::array<std::uint16_t,2048> angleTable{},magnitudeTable{};
};
struct RenderCameraInputs {
    std::array<std::uint32_t,3> worldPosition{};
    std::uint16_t pitch{},yaw{},roll{},averageWheelVelocity{};
};
struct RenderProjectionCamera {
    std::array<std::uint32_t,3> worldPosition{};
    std::uint16_t quarterTurn{},heightBaseline{},yawResidual{},viewportCenterY{};
    // A replay renderer may use the already evolved physics matrix words16/17
    // here: the original computes that roll pair before angle integration.
    std::uint16_t sinRoll{},cosRoll{},sinYaw{},cosYaw{};
    std::uint32_t rotatedXUnits{},rotatedZUnits{};
};
// Common world units with the simulation and decoded road mesh: one unit is
// one high word of cameraWorldX/Y/Z. A grid cell is128 units; mesh localX/Z
// words divide by16 and raw height words divide by32. The original display's
// vertical scale is half its horizontal scale; projection handles this.
struct RenderWorldPoint { double x{},y{},z{}; };
struct RenderCameraRelative { double side{},down{},forward{}; };
struct RenderProjectedPoint {
    int x{},y{};
    std::uint16_t radialDistance{};
    bool inFront{};
};
// Recover the original camera parameters on local typed state. No RAM,
// simulation state, or caller input is changed. Roll uses the same original
// table interpolation and Q15 coefficients as the physics matrix producer.
RenderProjectionCamera makeRenderProjectionCamera(const RenderCameraInputs&,const TrigTable&);
// Exact original word pipeline: X/Z inputs to0x6249c, vertical-down input to
//0x624f0, then roll/center0x62518. HeightDown is already shifted as in the road
// transform: signed(cameraHeightBaseline-rawHeight)>>3. Signed-word minimum
// magnitudes are outside the valid original geometry/division calling domain.
RenderProjectedPoint projectRenderCoordinates(std::uint16_t x,std::uint16_t z,std::uint16_t heightDown,
    const RenderProjectionCamera&,const RenderProjectionTables&);
RenderProjectedPoint projectRenderWorldPoint(const RenderWorldPoint&,const RenderProjectionCamera&,const RenderProjectionTables&);
// Yaw-oriented coordinates for viewer near-plane clipping, before angular
// pitch and screen-space roll. Near distance and polygon clipping remain
// explicit viewer choices; no claim of original visibility-queue recovery.
RenderCameraRelative renderCameraRelative(const RenderWorldPoint&,const RenderProjectionCamera&) noexcept;
// Auxiliary presentation depth for already quarter-turned original coordinates.
// Dense road uses 16 coordinate units per world unit; cached far road uses 8.
double renderCoordinateInverseDepth(std::uint16_t x,std::uint16_t z,std::uint16_t yawResidual,double units) noexcept;
} // namespace scr

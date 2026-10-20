#pragma once
#include "near_track_visibility.h"
#include <array>

namespace scr {
// Original wheel-footprint order: front, rear-left, rear-right's two corners
// is serialized as X1,Y1,X2,Y2 at0x1bf64. These are raw height words, not
// world-space mesh units. X/Z are the already transformed road coordinates.
struct OpponentCarInputs {
    std::array<std::uint16_t,4> footprint{},relativeX{},relativeZ{};
    std::array<std::uint16_t,3> bounds{};
    RenderProjectionCamera camera;
    std::uint16_t distance{},roadOffset{},lineBytes{};
    std::uint8_t aheadFlag{},curveSmoothing{},scaledPerspective{};
};
struct OpponentCarFrame {
    std::array<std::uint16_t,4> clampedHeights{};
    std::array<NearTrackPoint,4> footprintPoints{},clampedPoints{};
    // Procedural screen-space model at original vertex offsets0x100..0x11e.
    std::array<NearTrackPoint,16> vertices{};
    // Original render-command area0x5e0..0x66f, in four-byte edge slots.
    std::array<NearTrackEdge,36> edges{};
    std::vector<NearTrackDrawCommand> commands;
    std::uint16_t lineBytes{};
    bool admitted{};
};
// Original0x67aa6 guards and0x599e2 wheel-based procedural car construction.
// Geometry is recovered code; all angular tables remain local-pack assets.
OpponentCarFrame buildOpponentCarFrame(const OpponentCarInputs&,const RenderProjectionTables&);

enum class OpponentCarPainterSlot : std::uint8_t {ahead,behind,occluded};
// Original0x6916c/0x6917c/0x69188: the first matching painter slot consumes the
// opponent queue marker even when the car is outside its distance/angle guard.
bool consumeOpponentCarPainterSlot(OpponentCarPainterSlot,std::uint8_t relativePosition,
    std::uint16_t currentSegmentQueueOffset,std::uint16_t& opponentQueueOffset) noexcept;
} // namespace scr

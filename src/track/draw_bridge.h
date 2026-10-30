#pragma once
#include <array>
#include <cstdint>

namespace scr {
// Original disk routine0x5a794. Tables are loaded from the user's asset pack;
// they are data inputs, never reconstructed constants in the native program.
struct DrawBridgeData {
    std::array<std::uint8_t,16> angleTable{};
    std::array<std::uint8_t,16> opponentHeightOffsets{};
    std::uint16_t geometryOffset{};
    std::uint16_t firstSegmentBase{},secondSegmentBase{};
};
struct DrawBridgeInputs {
    std::uint8_t trackId{},playerSegment{},opponentSegment{},frameThrottleFlag{};
};
struct DrawBridgeState {
    std::uint8_t animationPhase{},active{},updateFlag{},scratchByte{};
    std::uint16_t segmentProgressDistance{},stepHeight{},rawTrackDataOffset{};
    std::array<std::uint8_t,2> rampAngles{};
    std::array<std::uint8_t,3> opponentHeights{};
    std::array<std::uint8_t,0x48> geometry{};
    std::array<std::uint16_t,8> cachedHeights{};
};
struct DrawBridgeUpdate { bool geometryUpdated{},cacheUpdated{}; };
// Advance exactly once at the original caller's update rate. Cars occupying
// the active bridge segments hold its geometry while opponent guidance still
// updates. frameThrottleFlag's sign bit suppresses the phase increment only.
DrawBridgeUpdate animateDrawBridge(const DrawBridgeData& data,
    const DrawBridgeInputs& inputs,DrawBridgeState& state);
} // namespace scr

#pragma once
#include "presentation/ghost_bridge.h"
#include "render/render_projection.h"
#include "race/race_timing.h"
#include <cstdint>
#include <vector>

namespace scr::runtime {
constexpr unsigned maxGhostLapSamples=65536,maxGhostLapBridgeShapes=64;
constexpr std::uint16_t noGhostBridge=0xffff;
struct GhostLapSample {
    std::int64_t microseconds{};
    RenderCameraInputs pose;
    std::uint16_t bridge{noGhostBridge};
    GhostBridgeRegion region{};
    std::uint8_t segment{};
    bool cut{};
};
struct GhostLapBoundary {
    std::int64_t observedMicroseconds{},alignedMicroseconds{};
    std::uint16_t progress{};
    std::uint8_t correctionHundredths{};
    bool interpolated{},recovery{};
};
struct LapRecording {
    PackedLapTime completedTime{};
    std::int64_t durationMicroseconds{};
    GhostLapBoundary start,finish;
    std::vector<GhostLapSample> samples;
    std::vector<GhostBridgeShape> bridges;
};
} // namespace scr::runtime

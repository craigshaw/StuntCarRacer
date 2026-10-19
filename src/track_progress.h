#pragma once
#include "track_sampling.h"
#include <array>
#include <cstdint>

namespace scr {
struct TrackProgressData {
    TrackGeometryData track;
    std::array<std::uint8_t,256> grid{};
    std::array<std::uint8_t,100> segmentCoordinates{};
    std::array<std::uint16_t,2048> angleTable{},magnitudeTable{};
    std::array<std::uint8_t,12> perspectiveFactors{};
};
struct TrackProgressInputs {
    std::uint16_t pitch{},yaw{},averageWheelVelocity{};
    // Configuration writes only the high bytes, but geometry reads full words.
    std::uint8_t slopeLowByte{},inversionLowByte{};
};
struct TrackProgressState {
    TrackSegmentConfiguration segment;
    std::array<std::uint32_t,3> worldPosition{};
    std::uint32_t rotatedX{},rotatedZ{};
    std::uint16_t cameraRotation{},carHeight{},cameraHeight{},cameraYaw{};
    std::uint16_t baseX{},baseZ{},trackRotation{},temporaryX{},temporaryZ{};
    std::uint16_t radialAngle{},radialDistance{},lateralOrigin{},lateralOffset{},forwardOrigin{},targetHeading{};
    std::uint16_t progression{},progressOffset{},visibility{},previousVisibility{};
    std::uint8_t viewX{},viewZ{},subGridX{},subGridZ{},offsetXHigh{},offsetZHigh{};
    std::uint8_t transformedGridX{},transformedGridZ{},gridLookupX{},gridLookupZ{};
    std::uint8_t currentSegment{},playerSegment{},lastValidSegment{};
    std::uint8_t temporarySegment{},scratch3{},scratch4{};
    std::uint8_t legacyRenderIndex{},renderIndex{},depth{},previousProgress{},previousDepth{};
    std::uint8_t offTrackFlags{},offRoadSide{},trackSide{},restartCountdown{},raceCompletionCheck{};
    std::array<std::uint16_t,32> debrisY{};
};

// Camera transform, track-cell lookup (including diagonal fallback), geometry,
// boundaries and progress: the local practice gameplay prefix before rendering.
// Returns whether a segment was found. Off-grid retains the previous segment
// and restores cached rendering progress, as the original does. World X/Z low
// words equal to zero become one. Input track topology must describe its grid;
// native bounds checks reject malformed data rather than inventing game behavior.
[[nodiscard]] bool updateTrackProgress(const TrackProgressData& data,
    const TrackProgressInputs& inputs,TrackProgressState& state);
} // namespace scr

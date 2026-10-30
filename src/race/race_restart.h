#pragma once
#include "track/track_progress.h"
#include "physics/transform_matrices.h"
#include <array>
#include <cstdint>

namespace scr {
struct RaceRestartData {
    TrackGeometryData track;
    std::array<std::uint8_t,100> segmentCoordinates{};
    std::array<std::uint8_t,16> geometryRestartFlags{};
    std::array<std::uint8_t,256> features{};
    std::uint8_t featureCount{};
};
// These are the vehicle fields cleared by the original restart loop, in their
// original word order. Explicit groups keep the reset's previously unnamed
// words as well as velocity, spring and collision fields observable.
struct RestartVehicleDynamics {
    std::uint32_t rotatedX{};
    std::uint16_t carHeight{},reservedHeight{};
    std::uint32_t rotatedZ{};
    std::array<std::uint32_t,3> worldPosition{};
    std::array<std::uint16_t,3> angles{},worldVelocity{},rotationVelocity{};
    std::array<std::uint16_t,3> worldAcceleration{},angularAcceleration{};
    std::array<std::uint16_t,6> wheelCoordinates{};
    std::uint16_t baseTargetRoll{},baseRollAngle{},additionalYawOffset{};
    std::array<std::uint16_t,3> clampedSuspension{},previousSuspension{},suspensionVelocity{};
    std::uint16_t targetPitchRate{},targetRollRate{},gasOutput{};
    std::array<std::uint16_t,3> localVelocity{},adjustedAngles{};
    std::uint16_t averageWheelVelocity{};
    std::array<std::uint16_t,3> transformedAngularVelocity{},springAngles{};
    std::uint16_t reservedSpring{};
    std::array<std::uint16_t,3> springDisplacements{},springComponents{},opponentCollisionForce{};
    std::uint16_t targetHeading{},speedMagnitude{};
};
struct RaceRestartState {
    RestartVehicleDynamics vehicle;
    TrackSegmentConfiguration segment;
    std::array<std::uint32_t,3> wheelHeights{},trackSurfaceHeights{};
    std::uint16_t trackSurfaceHeight{};
    std::uint8_t currentSegment{},playerSegment{},gridLookupX{},gridLookupZ{};
    std::uint8_t raceStartTimer{},trackSide{},trackSideCopy{},multiplierScratch{};
    std::uint8_t chainVerticalPosition{},chainLiftVelocity{};
};
// Original [0x605b6,0x606bc): reject unsafe/feature segments by walking
// backwards, clear the complete vehicle state, load geometry, and place its
// center in the track grid. Does not start a race or reset lap bookkeeping.
void preparePlayerRestart(const RaceRestartData& data,std::uint8_t requestedSegment,RaceRestartState& state);
// Original [0x606d4,0x60754): use the freshly sampled rear height and matrix
// words4/6, place the suspended car, and clear previous suspension travel.
void finishPlayerRestart(bool raceStartComplete,const TransformMatrices& matrices,RaceRestartState& state) noexcept;
// Original applyCameraLateralOffset [0x5b506,0x5b56c).
void applyRestartLateralOffset(const TransformMatrices& matrices,RaceRestartState& state) noexcept;
// Original [0x6075a,0x6076c). Original velocity is8; enhanced reference divides it.
void finishRestartChain(RaceRestartState& state) noexcept;
void initializeRestartDebris(std::array<std::uint16_t,32>& positions) noexcept;
} // namespace scr

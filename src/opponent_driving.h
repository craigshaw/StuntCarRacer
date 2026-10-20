#pragma once
#include "track_progress.h"
#include <array>
#include <cstdint>

namespace scr {
struct OpponentDrivingData {
    TrackProgressData track;
    std::array<std::uint8_t,100> visibility{};
    std::array<std::uint8_t,32> traits{};
    std::array<std::uint8_t,16> patterns{};
    // The original signed-word index can address[-2048,2047] after ASR#4.
    // Preserve that source-backed window, including the NEG.W8000 edge.
    std::array<std::uint8_t,4096> distanceFactors{};
    std::array<std::uint16_t,3> suspensionDamping{};
    std::uint8_t lookahead{};
};
struct OpponentDrivingInputs {
    std::uint8_t raceStartComplete{},crashRecovery{},network{},frameThrottle{},opponentId{};
    std::uint8_t baseSpeedHigh{},baseSpeedLow{};
    std::uint8_t slopeLowByte{};
};
struct OpponentDrivingState {
    // Shared configuration/camera and scratch fields are explicit typed state;
    // the adapter alone knows their original memory representation.
    TrackProgressState progress;
    std::uint32_t randomSeed{};
    std::uint8_t randomTail{},segment{},speedFraction{},enabled{},ahead{},drafting{};
    std::uint8_t actionTimer{},patternOffset{},patternControl{},movementOverride{};
    std::array<std::uint8_t,2> segmentOffsets{};
    std::uint8_t boundaryCollision{},collisionDistance{},lateral{};
    std::uint16_t subSegment{},speed{},targetSpeed{},minimumBoundary{};
    std::array<std::uint16_t,3> acceleration{},previousHeight{},velocity{},wheelForce{};
    std::array<std::uint16_t,4> wheelPosition{};
    std::array<std::uint16_t,4> bounds{},footprint{};
    std::array<std::uint16_t,16> coordinatesX{},coordinatesZ{};
    std::array<std::uint16_t,6> controls{};
    std::uint16_t trackDistance{},behindProgress{},heightDifference{},adjustedDistance{},blendFactor{};
    std::uint16_t perpendicular{},attenuated{},distanceOffset{};
    std::uint32_t interpolationScratch{};
    std::uint8_t trackDirection{},behindSegment{},distanceFactor{},segmentHalf{},blend{},curveSmoothing{},trackOffsetBase{};
    std::uint8_t scratch1{},scratch2{}; // 1bb18..19, overlap interpolationScratch
};

// Original processOpponentLogic0x63996, including wheel dynamics, AI pattern,
// speed selection, avoidance and subsegment integration. Caller owns the
// Practice/competitive gate; race-start and recovery gates are retained here.
void updateOpponentLogic(const OpponentDrivingData&,const OpponentDrivingInputs&,OpponentDrivingState&);
// Original setupTrackGeometryForFrame0x5a186. Generates the original wheel
// support heights and car footprint in the current camera coordinate system.
void setupOpponentTrackGeometry(const OpponentDrivingData&,const OpponentDrivingInputs&,OpponentDrivingState&);
// Original initializeTrackCoordinates0x63a58, including exactly one RNG call.
void initializeOpponentCoordinates(const OpponentDrivingData&,const OpponentDrivingInputs&,OpponentDrivingState&);
} // namespace scr

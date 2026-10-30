#pragma once
#include <array>
#include <cstdint>

namespace scr {

struct RollTransitionInputs {
    std::uint16_t baseTargetRoll{}, rollSpringAngle{}, carLocalVelocityX{}, pitchSpringAngle{};
    std::uint8_t wheelMovementActive{};
};
struct RollTransitionState {
    std::uint16_t adjustedRollAngle{};
    std::uint8_t rollTransitionFlag{};
};

// Names follow the matched disassembly; their broader physical interpretation
// remains provisional. All words retain original signed bits and word wrapping.
std::uint16_t getSteeringMultiplier(std::uint16_t pitchSpringAngle,
                                    std::uint8_t wheelMovementActive) noexcept;
void calculateRollAngleTransition(const RollTransitionInputs& inputs,
                                  RollTransitionState& state) noexcept;

struct CarOrientationInputs {
    std::uint16_t baseRollAngle{}, pitchSpringAngle{}, yawSpringAngle{}, additionalYawOffset{};
    std::uint16_t baseTargetRoll{}, rollSpringAngle{}, carLocalVelocityX{}, carLocalVelocityZ{};
    std::uint8_t wheelMovementActive{};
};
struct CarOrientationState {
    // gasOutputIntensityValue is this word's low byte, not an independent input.
    std::uint16_t gasOutputAccumulator{}, adjustedPitchAngle{}, adjustedYawAngle{};
    RollTransitionState roll{};
};
void updateCarOrientation(const CarOrientationInputs& inputs, CarOrientationState& state) noexcept;

struct CarDragInputs {
    std::array<std::uint16_t,3> localVelocity{}, worldVelocity{};
    std::uint8_t wheelMovementActive{}, collisionResponseHighByte{}, offTrackStateFlags{};
    // Original 0x1bca2 also aliases byte 2 of trackOffsetAdjustment's longword.
    std::uint8_t postWreckStateFlag{}, raceStartTimer{}, draftingTimer{}, opponentAheadFlag{};
};
struct CarDragState { std::array<std::uint16_t,3> worldAcceleration{}; };

// Original updateCollisionState [0x621f4,0x622dc) only changes these three
// acceleration words. Collision-response flags and velocities are inputs.
void updateCollisionDrag(const CarDragInputs& inputs, CarDragState& state) noexcept;

} // namespace scr

#include "car_dynamics.h"

namespace scr {
namespace {
constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value < 0x8000 ? std::int32_t(value) : std::int32_t(value) - 65536;
}
constexpr std::uint16_t word(std::int32_t value) noexcept { return static_cast<std::uint16_t>(value); }
constexpr std::uint16_t negate(std::uint16_t value) noexcept { return word(-std::int32_t(value)); }
constexpr std::uint16_t magnitude(std::uint16_t value) noexcept {
    return value & 0x8000 ? negate(value) : value;
}
constexpr std::int32_t floorDivide(std::int32_t value,std::int32_t divisor) noexcept {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}
} // namespace

std::uint16_t getSteeringMultiplier(std::uint16_t pitchSpringAngle,
                                    std::uint8_t wheelMovementActive) noexcept {
    // Original [0x621da,0x621f4): ASL.W wraps before either caller compares it.
    return wheelMovementActive ? word(std::int32_t(pitchSpringAngle) * 2) : std::uint16_t{0};
}

void calculateRollAngleTransition(const RollTransitionInputs& input,
                                  RollTransitionState& state) noexcept {
    // Original [0x6217a,0x621da). NEG.W of 0x8000 is deliberately unchanged.
    const auto target = word(std::int32_t(input.baseTargetRoll) + input.rollSpringAngle);
    const auto difference = magnitude(word(std::int32_t(target) - input.carLocalVelocityX));
    auto multiplier = getSteeringMultiplier(input.pitchSpringAngle,input.wheelMovementActive);
    if (difference < multiplier) { // Original BCS is an unsigned comparison.
        state.adjustedRollAngle = word(std::int32_t(input.rollSpringAngle) - input.carLocalVelocityX);
        state.rollTransitionFlag = 0;
    } else {
        if (input.carLocalVelocityX & 0x8000) multiplier = negate(multiplier);
        state.adjustedRollAngle = word(std::int32_t(target) - multiplier);
        state.rollTransitionFlag = 0x80;
    }
}

void updateCarOrientation(const CarOrientationInputs& input, CarOrientationState& state) noexcept {
    // Original [0x620b8,0x62138), including its two recovered callees.
    state.adjustedPitchAngle = word(std::int32_t(input.baseRollAngle) + input.pitchSpringAngle);
    const auto combinedHighByte = static_cast<std::uint8_t>(
        (state.gasOutputAccumulator >> 8) | (input.carLocalVelocityZ >> 8));
    if (!(combinedHighByte & 0x80) && (state.gasOutputAccumulator & 0xff))
        state.gasOutputAccumulator = word(std::int32_t(state.gasOutputAccumulator) - combinedHighByte);

    const auto multiplier = getSteeringMultiplier(input.pitchSpringAngle,input.wheelMovementActive);
    if (magnitude(state.gasOutputAccumulator) >= multiplier)
        state.gasOutputAccumulator = state.gasOutputAccumulator & 0x8000 ? negate(multiplier) : multiplier;
    state.adjustedYawAngle = word(std::int32_t(state.gasOutputAccumulator)
        + input.yawSpringAngle + input.additionalYawOffset);
    calculateRollAngleTransition({input.baseTargetRoll,input.rollSpringAngle,input.carLocalVelocityX,
                                  input.pitchSpringAngle,input.wheelMovementActive},state.roll);
}

void updateCollisionDrag(const CarDragInputs& input, CarDragState& state) noexcept {
    // Original [0x621f4,0x622dc). For a negative collision byte the original
    // uses one's complement, so FF -> 0, FE -> 1, FD -> 2, FC -> 3.
    const auto collisionMagnitude = static_cast<std::uint8_t>(input.collisionResponseHighByte & 0x80
        ? input.collisionResponseHighByte ^ 0xff : input.collisionResponseHighByte);
    std::uint16_t coefficient = 0x6000;
    unsigned shift = 1;
    if (input.wheelMovementActive && (collisionMagnitude >= 3 || (input.offTrackStateFlags & 0x80))) {
        // Strong drag path, independent of countdown/post-wreck flags.
    } else if ((input.wheelMovementActive && input.postWreckStateFlag) || input.raceStartTimer) {
        shift = 3;
    } else {
        coefficient = magnitude(input.localVelocity[0]);
        for (unsigned axis = 1; axis < 3; ++axis) {
            const auto candidate = magnitude(input.localVelocity[axis]);
            // The maximum comparison is signed; 0x8000 remains negative.
            if (signedWord(coefficient) < signedWord(candidate)) coefficient = candidate;
        }
        shift = 5;
        if ((input.draftingTimer & 0x80) && !(input.opponentAheadFlag & 0x80))
            coefficient = coefficient < 0x0a00 ? std::uint16_t{0} : word(std::int32_t(coefficient) - 0x0a00);
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto product = signedWord(input.worldVelocity[axis]) * signedWord(coefficient);
        const auto highWord = floorDivide(product,65536); // MULS; SWAP; read low word.
        const auto drag = floorDivide(highWord,std::int32_t{1} << shift);
        state.worldAcceleration[axis] = word(std::int32_t(state.worldAcceleration[axis]) - drag);
    }
}

} // namespace scr

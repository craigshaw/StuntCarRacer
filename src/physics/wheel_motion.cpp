#include "physics/wheel_motion.h"

namespace scr {
namespace {
constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value < 0x8000 ? std::int32_t(value) : std::int32_t(value) - 65536;
}
constexpr std::int32_t floorDivide(std::int32_t value, std::int32_t divisor) noexcept {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}
} // namespace

void updateWheelSpeed(const WheelSpeedInputs& input, WheelSpeedState& state) noexcept {
    // Original [0x60fbe,0x61012). NEG.W preserves the exceptional 0x8000.
    const auto magnitude = input.carLocalVelocityZ < 0x8000
        ? input.carLocalVelocityZ : static_cast<std::uint16_t>(-std::int32_t(input.carLocalVelocityZ));
    state.carSpeedMagnitude = magnitude;
    if (!input.wheelMovementActive) {
        // Original LSR.W #2, not the enhanced reference's #5.
        state.wheelSpeed = static_cast<std::uint16_t>(state.wheelSpeed - state.wheelSpeed / 4);
    } else if (signedWord(magnitude) < 0x800) {
        state.wheelSpeed = static_cast<std::uint16_t>(std::uint32_t(magnitude) * 8);
    } else {
        const auto doubled = static_cast<std::uint16_t>(std::uint32_t(magnitude) * 2);
        const auto biased = std::uint32_t(doubled) + 0x3000;
        // BCC tests the ADD.W carry, not ASL.W's earlier carry.
        state.wheelSpeed = biased > 0xffff ? std::uint16_t{0xff00} : static_cast<std::uint16_t>(biased);
    }
}

void updateVelocityDamping(const VelocityDampingInputs& input,
                           VelocityDampingState& state) noexcept {
    // Original [0x62138,0x6217a): both rotation rates use ASR.W #4
    // regardless of wheel movement. Only the yaw contribution is conditional.
    auto pitch = std::int32_t(input.targetPitchRate) - floorDivide(signedWord(input.rotationSpeedX), 16);
    if (input.wheelMovementActive) pitch += floorDivide(signedWord(input.adjustedYawAngle), 4);
    state.angularAccelerationX = static_cast<std::uint16_t>(pitch);
    state.angularAccelerationZ = static_cast<std::uint16_t>(std::int32_t(input.targetRollRate)
        - floorDivide(signedWord(input.rotationSpeedZ), 16));
}

} // namespace scr

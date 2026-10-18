#pragma once
#include <cstdint>

namespace scr {

// Raw 16-bit game words retain signed arithmetic and modulo-65536 results.
struct WheelSpeedInputs {
    std::uint16_t carLocalVelocityZ;
    std::uint8_t wheelMovementActive;
};
struct WheelSpeedState {
    std::uint16_t carSpeedMagnitude;
    std::uint16_t wheelSpeed;
};

void updateWheelSpeed(const WheelSpeedInputs& input, WheelSpeedState& state) noexcept;

struct VelocityDampingInputs {
    std::uint16_t rotationSpeedX, rotationSpeedZ;
    std::uint16_t targetPitchRate, targetRollRate, adjustedYawAngle;
    std::uint8_t wheelMovementActive;
};
struct VelocityDampingState {
    std::uint16_t angularAccelerationX, angularAccelerationZ;
};

void updateVelocityDamping(const VelocityDampingInputs& input,
                           VelocityDampingState& state) noexcept;

} // namespace scr

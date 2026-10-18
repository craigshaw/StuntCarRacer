#ifndef SCR_MOTION_INTEGRATION_H
#define SCR_MOTION_INTEGRATION_H

#include <array>
#include <cstdint>

namespace scr {

using MotionVector=std::array<std::uint16_t,3>; // X, Y, Z; signed word bits.
// Original four big-endian words at [0x61ad4,0x61adc): normal/special positive
// limits, followed by normal/special negative limits. Supplied game data.
using MotionAngleLimits=std::array<std::uint16_t,4>;

struct MotionIntegrationState {
    std::array<std::uint32_t,3> worldPosition{};
    MotionVector angles{},worldVelocity{},rotationVelocity{};
    std::uint8_t steepPitchFlag{};
    std::uint16_t viewportCenterY{};
};

struct MotionPositionInputs {
    MotionVector transformedAngularVelocity{};
    std::uint8_t raceCompletionFlag{},renderingOrderMode{};
};

// Original [0x61adc,0x61b26) and [0x61b26,0x61b70). Both use the disk's
// timestep 238/256, rounding down before the wrapped word addition.
void integrateWorldVelocity(const MotionVector& acceleration,MotionIntegrationState& state) noexcept;
void integrateAngularVelocity(const MotionVector& acceleration,MotionIntegrationState& state) noexcept;

// Original [0x61950,0x61ad4). Adds world/angle movement, clamps the world's Y
// high word and pitch/roll, cancels outward rotation at angle limits, and updates
// the pitch flag/viewport word. Inputs and state retain original integer bits.
// All three original routines require D2 bits8..15 zero before their first
// byte-sized timestep write; this native contract uses that calling convention.
// CPU registers/CCR, timing, velocity transformation and the full physics tick
// are outside these functions. These are fixed original-step calculations.
void updateWorldPosition(const MotionPositionInputs& inputs,const MotionAngleLimits& limits,
                         MotionIntegrationState& state) noexcept;

} // namespace scr
#endif // SCR_MOTION_INTEGRATION_H

#ifndef SCR_SUSPENSION_RESPONSE_H
#define SCR_SUSPENSION_RESPONSE_H

#include "physics/suspension_springs.h"
#include <array>
#include <cstdint>

namespace scr {

// Array order throughout is front left, front right, rear. Longwords and words
// carry original two's-complement bits; no physical unit conversion is implied.
struct SuspensionResponseInputs {
    std::array<std::uint32_t, 3> wheelHeight;
    std::array<std::uint32_t, 3> expectedHeight;
    std::uint32_t trackOffsetAdjustment;
    std::uint8_t trackWidthMultiplier;
    std::uint8_t frameThrottleFlag;
    std::uint8_t damageFrameLimit;
    std::uint8_t raceStartTimer;
    std::uint16_t pitchAngle;
    std::uint8_t trackId;
    std::uint16_t rotationSpeedX;
};

struct WheelSuspension {
    std::uint32_t travel{};
    std::uint16_t clampedTravel{};
    std::uint16_t previousTravel{};
    std::uint16_t velocity{};
    std::uint8_t accumulatedForce{};
};

struct SuspensionResponseState {
    std::array<WheelSuspension, 3> wheels;
    std::uint16_t maxCompressionVelocity{}; // Entry clears its high byte only.
    std::uint8_t hardImpactCount{};
    std::uint8_t highCompressionFrameCount{}; // Shared, updated once per wheel.
    std::uint8_t damageAccumulationActive{};
    std::uint16_t frontAverageVelocity{}; // Original tempWord1 scratch.
    std::uint16_t averageVelocity{};
    std::uint16_t targetRollRate{};
    std::uint16_t targetPitchRate{};
    std::uint8_t wheelMovementActive{};
    SuspensionSpringState springs;
};

// Original [0x61bcc,0x62042), ending before handleRaceStartCountdown. Includes
// original momentum helper and the recovered spring stage. Countdown, opponent
// collision and audio after this boundary remain outside this function.
// Uses the spring stage's normal-game D1-high-byte-zero entry contract.
void updateSuspensionResponse(const SuspensionResponseInputs& input,
                              const SuspensionAttenuationTable& attenuation,
                              SuspensionResponseState& state) noexcept;

} // namespace scr
#endif

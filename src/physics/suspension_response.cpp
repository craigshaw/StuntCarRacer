#include "physics/suspension_response.h"

namespace scr {
namespace {

constexpr std::int32_t signedWord(std::uint16_t v) noexcept {
    return v < 0x8000 ? std::int32_t(v) : std::int32_t(v) - 65536;
}
constexpr std::int32_t signedByte(std::uint8_t v) noexcept {
    return v < 0x80 ? std::int32_t(v) : std::int32_t(v) - 256;
}
constexpr std::uint16_t word(std::int32_t v) noexcept { return static_cast<std::uint16_t>(v); }
constexpr std::uint16_t negate(std::uint16_t v) noexcept { return word(-std::int32_t(v)); }
constexpr std::int32_t floorDivide(std::int32_t v, std::int32_t divisor) noexcept {
    return v / divisor - (v % divisor < 0 ? 1 : 0);
}
constexpr std::uint16_t halfWord(std::uint16_t v) noexcept { return word(floorDivide(signedWord(v), 2)); }

std::uint16_t clampTravel(std::uint32_t travel) noexcept {
    if (travel & 0x80000000u) {
        if (travel < 0xfffffd00u) travel = 0xfffffd00u;
    } else if (travel >= 0x1400) travel = 0x1400;
    return static_cast<std::uint16_t>(travel);
}

void updateWheel(const SuspensionResponseInputs& input, unsigned index,
                 SuspensionResponseState& state) noexcept {
    auto& wheel = state.wheels[index];
    wheel.travel = input.wheelHeight[index] - input.expectedHeight[index] - input.trackOffsetAdjustment;
    wheel.clampedTravel = clampTravel(wheel.travel);
    const auto delta = word(std::int32_t(wheel.clampedTravel) - wheel.previousTravel);
    // Original helper [0x6180e,0x6181e): multiplier is 0x114, with ASR.L #8.
    // Its final ADD.W sets the sign tested by the caller's BMI after JSR.
    const auto amplified = floorDivide(signedWord(delta) * 0x114, 256);
    const auto velocity = word(amplified + wheel.clampedTravel);
    if (velocity & 0x8000) {
        wheel.velocity = 0;
        state.highCompressionFrameCount = 0;
    } else {
        if (velocity >= 0x400 && signedWord(wheel.velocity) < 0x200)
            ++state.hardImpactCount;
        wheel.velocity = velocity;
        const auto compression = word(std::int32_t(velocity) - std::int32_t(input.trackWidthMultiplier) * 256);
        if (signedWord(compression) < 0x700) {
            state.highCompressionFrameCount = 0;
            // This original branch skips the later velocity cap as well.
        } else {
            if (compression >= state.maxCompressionVelocity) state.maxCompressionVelocity = compression;
            if (!(input.frameThrottleFlag & 0x80)) {
                ++state.highCompressionFrameCount; // Byte wraps; comparison below is signed.
                if (signedByte(state.highCompressionFrameCount) < signedByte(input.damageFrameLimit)) {
                    const auto high = (compression - 0x600u) / 256;
                    const auto increment = static_cast<std::uint8_t>(high + high / 2);
                    const auto total = unsigned(wheel.accumulatedForce) + increment;
                    wheel.accumulatedForce = static_cast<std::uint8_t>(total > 255 ? 255 : total);
                    state.damageAccumulationActive = 0x80;
                }
            }
            if (wheel.velocity >= 0x1200) wheel.velocity = 0x11ff;
        }
    }
    wheel.previousTravel = wheel.clampedTravel;
}

} // namespace

void updateSuspensionResponse(const SuspensionResponseInputs& input,
                              const SuspensionAttenuationTable& attenuation,
                              SuspensionResponseState& state) noexcept {
    state.hardImpactCount = 0;
    state.maxCompressionVelocity &= 0x00ff; // MOVE.B zero at big-endian word address.
    for (unsigned index = 0; index < 3; ++index) updateWheel(input, index, state);

    state.frontAverageVelocity = halfWord(word(std::int32_t(state.wheels[0].velocity) + state.wheels[1].velocity));
    state.averageVelocity = halfWord(word(std::int32_t(state.frontAverageVelocity) + state.wheels[2].velocity));
    calculateSuspensionSpringForces({state.wheels[0].travel, state.wheels[1].travel,
                                    state.wheels[2].travel, state.averageVelocity}, attenuation, state.springs);

    const auto difference = word(std::int32_t(state.wheels[0].velocity) - state.wheels[1].velocity);
    auto roll = word(std::int32_t(difference) * 3);
    if (roll & 0x8000) roll = negate(roll);
    // NEG.W of 0x8000 stays negative, so the signed comparison must stay signed.
    if (signedWord(roll) >= 0x1000) roll = 0x1000;
    if (difference & 0x8000) roll = negate(roll);
    state.targetRollRate = roll;
    state.targetPitchRate = word(std::int32_t(state.frontAverageVelocity) - state.wheels[2].velocity);
    state.wheelMovementActive = static_cast<std::uint8_t>((state.averageVelocity >> 8) | state.averageVelocity);
    if (state.wheelMovementActive || input.raceStartTimer) return;

    std::uint16_t reference = 0xff80;
    if (input.pitchAngle & 0x8000) {
        if (input.trackId == 4) reference = 0xfff8;
        else if (input.trackId != 7) return;
        // Track 7 writes F8 to D1, not D3. It leaves the -128 reference intact.
    } else if (input.pitchAngle >= 0x1000) reference = 0xff00;
    const auto adjustment = word(std::int32_t(reference) - state.targetPitchRate);
    if (!(adjustment & 0x8000)) return;
    const auto rotationHigh = static_cast<std::uint8_t>(input.rotationSpeedX >> 8);
    if (!(rotationHigh & 0x80) || rotationHigh == 0xff) state.targetPitchRate = adjustment;
}

} // namespace scr

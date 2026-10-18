#include "suspension_springs.h"

namespace scr {
namespace {

constexpr std::uint32_t arithmeticShift(std::uint32_t word, unsigned count) noexcept {
    // Only positive counts 1, 3 and 4 occur here. Work on bits to avoid signed
    // right-shift assumptions and signed overflow in the preceding arithmetic.
    return (word >> count) | ((word & 0x80000000u) ? (~std::uint32_t{0} << (32 - count)) : 0);
}

constexpr std::int32_t signedWord(std::uint16_t word) noexcept {
    return word < 0x8000 ? std::int32_t(word) : std::int32_t(word) - 0x10000;
}

void attenuate(std::uint16_t displacement, const SuspensionAttenuationTable& table,
               SuspensionSpringScratch& scratch) noexcept {
    if (displacement & 0x8000)
        displacement = static_cast<std::uint16_t>(-std::int32_t(displacement));
    // Original CMP.W/BGE is signed. NEG.W of -32768 remains 0x8000,
    // therefore this exceptional magnitude takes its low byte (zero).
    scratch.rawDisplacement = signedWord(displacement) >= 256
        ? std::uint8_t{255} : static_cast<std::uint8_t>(displacement);
    scratch.attenuation = table[scratch.rawDisplacement / 2];
}

std::uint8_t multiplyBytes(std::uint8_t value, SuspensionSpringScratch& scratch) noexcept {
    // The reference label multiplyAndRandomize is misleading: no randomness.
    const auto product = std::uint32_t(value) * scratch.multiplier;
    scratch.productLow = static_cast<std::uint8_t>(product);
    return static_cast<std::uint8_t>(product / 256);
}

std::uint16_t springAngle(std::uint16_t velocity, SuspensionSpringScratch& scratch) noexcept {
    auto coefficient = std::int32_t(scratch.multiplier) * 128;
    if (scratch.signByte & 0x80) coefficient = -coefficient;
    const auto product = signedWord(velocity) * coefficient; // Fits signed 32 bits.
    // MULS; ASL.L #1; SWAP; MOVE.W rounds the signed product down by 2^15.
    const auto result = product / 32768 - (product % 32768 < 0 ? 1 : 0);
    return static_cast<std::uint16_t>(result);
}

} // namespace

void calculateSuspensionSpringForces(const SuspensionSpringInputs& input,
                                    const SuspensionAttenuationTable& table,
                                    SuspensionSpringState& state) noexcept {
    // Original disk code, including helpers, is verified by
    // scripts/reference/verify_suspension_springs.py. Keep both longword wraps
    // and each arithmetic shift; averaging in a wider type changes overflow.
    state.pitchReferenceZero = 0;
    const auto averageFront = arithmeticShift(input.frontLeftTravel + input.frontRightTravel, 1);
    const auto pitch = static_cast<std::uint16_t>(arithmeticShift(averageFront - input.rearTravel, 4));
    state.pitchDisplacement = pitch ^ 0x8000;
    attenuate(pitch, table, state.scratch);
    state.scratch.savedAttenuation = state.scratch.attenuation;
    state.yawComponent.value = state.scratch.rawDisplacement;

    state.rollDisplacement = static_cast<std::uint16_t>(
        arithmeticShift(input.frontLeftTravel - input.frontRightTravel, 3));
    attenuate(state.rollDisplacement, table, state.scratch);
    state.scratch.multiplier = state.scratch.savedAttenuation;
    state.pitchComponent.value = multiplyBytes(state.scratch.attenuation, state.scratch);
    state.rollComponent.value = multiplyBytes(state.scratch.rawDisplacement, state.scratch);

    state.scratch.multiplier = state.rollComponent.value;
    state.scratch.signByte = static_cast<std::uint8_t>(state.rollDisplacement >> 8);
    state.rollAngle = springAngle(input.averageVelocity, state.scratch);
    state.scratch.multiplier = state.pitchComponent.value;
    state.scratch.signByte = 0; // High byte of pitchReferenceZero, cleared above.
    state.pitchAngle = springAngle(input.averageVelocity, state.scratch);
    state.scratch.multiplier = state.yawComponent.value;
    state.scratch.signByte = static_cast<std::uint8_t>(state.pitchDisplacement >> 8);
    state.yawAngle = springAngle(input.averageVelocity, state.scratch);
}

} // namespace scr

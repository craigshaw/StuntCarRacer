#include "physics/transform_matrices.h"
#include <cstddef>

namespace scr {
namespace {

constexpr std::int32_t signedWord(std::uint16_t word) noexcept {
    return word < 0x8000 ? std::int32_t(word) : std::int32_t(word) - 0x10000;
}

// MULS.W; ASL.L #1; SWAP; MOVE.W is a signed Q15 multiplication, rounding
// down at each multiply. Unsigned narrowing retains the original word bits.
constexpr std::uint16_t multiplyQ15(std::uint16_t a, std::uint16_t b) noexcept {
    const auto product = signedWord(a) * signedWord(b); // fits int32_t
    const auto rounded = product / 32768 - (product % 32768 < 0 ? 1 : 0);
    return static_cast<std::uint16_t>(rounded);
}

constexpr std::uint16_t negateWord(std::uint16_t word) noexcept {
    return static_cast<std::uint16_t>(-std::int32_t(word));
}

// Original entries: sine 0x64d08, cosine 0x64d10; shared body ends at 0x64d66.
// The stored table decreases from 65535 to zero over a quarter turn. Folding
// selects that quarter; the low six angle bits interpolate adjacent entries.
std::uint16_t lookupTrig(std::uint16_t angle, bool cosine, const TrigTable& table) noexcept {
    std::uint32_t phase = angle & 0x3fff;
    const bool secondQuarter = (angle & 0x4000) != 0;
    if (secondQuarter == cosine) phase = 0x4000 - phase;

    const auto index = phase / 64;
    const auto fraction = phase % 64;
    const auto decrease = static_cast<std::uint16_t>(table[index] - table[index + 1]);
    const auto interpolation = std::uint32_t(decrease) * fraction / 64;
    // Preserve SUB.W before the logical shift: this also documents unsigned
    // interpolation instead of silently treating the table as signed Q15.
    const auto interpolated = static_cast<std::uint16_t>(table[index] - interpolation);
    const auto magnitude = static_cast<std::uint16_t>(interpolated / 2);
    const bool negative = ((angle & 0x8000) != 0) != (cosine && secondQuarter);
    return negative ? negateWord(magnitude) : magnitude;
}

} // namespace

std::uint16_t gameSine(std::uint16_t angle, const TrigTable& table) noexcept {
    return lookupTrig(angle, false, table);
}

std::uint16_t gameCosine(std::uint16_t angle, const TrigTable& table) noexcept {
    return lookupTrig(angle, true, table);
}

WheelTransformWords TransformMatrices::wheelInputs() const noexcept {
    // Reference names matrix44/46/48 refer to actual offsets 42/44/46.
    return {words[0x34 / 2], words[0x38 / 2], words[0x3e / 2],
            words[0x42 / 2], words[0x44 / 2], words[0x46 / 2]};
}

void updateTransformMatrices(const TransformAngles& angles, const TrigTable& table,
                             TransformMatrices& matrices) noexcept {
    const auto sinYaw = gameSine(angles.yaw, table);
    const auto cosYaw = gameCosine(angles.yaw, table);
    const auto sinPitch = gameSine(angles.pitch, table);
    const auto cosPitch = gameCosine(angles.pitch, table);
    const auto sinRoll = gameSine(angles.roll, table);
    const auto cosRoll = gameCosine(angles.roll, table);
    const auto relativeHeading = static_cast<std::uint16_t>(angles.yaw - angles.trackHeading);
    const auto sinRelative = gameSine(relativeHeading, table);
    const auto cosRelative = gameCosine(relativeHeading, table);

    // Keep the original multiplication grouping: each step discards low bits.
    const auto sinYawSinPitch = multiplyQ15(sinYaw, sinPitch);
    const auto cosYawSinPitch = multiplyQ15(cosYaw, sinPitch);
    const auto sinYawSinPitchSinRoll = multiplyQ15(sinYawSinPitch, sinRoll);
    const auto sinYawSinPitchCosRoll = multiplyQ15(sinYawSinPitch, cosRoll);
    const auto cosYawSinPitchSinRoll = multiplyQ15(cosYawSinPitch, sinRoll);
    const auto cosYawSinPitchCosRoll = multiplyQ15(cosYawSinPitch, cosRoll);
    const auto sinYawSinRoll = multiplyQ15(sinYaw, sinRoll);
    const auto sinYawCosRoll = multiplyQ15(sinYaw, cosRoll);
    const auto cosYawSinRoll = multiplyQ15(cosYaw, sinRoll);
    const auto cosYawCosRoll = multiplyQ15(cosYaw, cosRoll);
    const auto cosPitchSinRoll = multiplyQ15(cosPitch, sinRoll);

    // Store into the original layout so later recovered routines can share it.
    // Narrowing sums/differences to unsigned words gives defined modulo-65536
    // behavior, including for coefficients outside the signed 16-bit range.
    const auto put = [&](std::size_t byteOffset, std::int32_t value) {
        matrices.words[byteOffset / 2] = static_cast<std::uint16_t>(value);
    };
    put(0x00, sinYawSinPitch);
    put(0x02, cosYawSinPitch);
    put(0x04, multiplyQ15(sinYaw, cosPitch));
    put(0x06, multiplyQ15(cosYaw, cosPitch));
    put(0x08, sinPitch);
    put(0x0a, cosPitch);
    put(0x0c, sinYawSinPitchSinRoll);
    put(0x0e, sinYawSinPitchCosRoll);
    put(0x10, cosYawSinPitchSinRoll);
    put(0x12, cosYawSinPitchCosRoll);
    put(0x14, sinYawSinRoll);
    put(0x16, sinYawCosRoll);
    put(0x18, cosYawSinRoll);
    put(0x1a, cosYawCosRoll);
    put(0x1c, cosPitchSinRoll);
    put(0x1e, multiplyQ15(cosPitch, cosRoll));
    put(0x20, sinRoll);
    put(0x22, cosRoll);
    put(0x24, negateWord(sinRoll));
    put(0x28, std::int32_t(cosYawSinRoll) - sinYawSinPitchCosRoll);
    put(0x2a, -std::int32_t(cosYawSinPitchCosRoll) - sinYawSinRoll);
    put(0x2c, std::int32_t(cosYawCosRoll) + sinYawSinPitchSinRoll);
    put(0x2e, std::int32_t(cosYawSinPitchSinRoll) - sinYawCosRoll);
    put(0x30, negateWord(cosPitchSinRoll));

    // Track-relative coefficients consumed immediately by the wheel routine.
    put(0x34, multiplyQ15(multiplyQ15(sinRelative, sinPitch), sinRoll));
    put(0x38, multiplyQ15(multiplyQ15(cosRelative, sinPitch), sinRoll));
    put(0x3e, multiplyQ15(cosRelative, cosRoll));
    put(0x42, multiplyQ15(sinRelative, cosRoll));
    put(0x44, multiplyQ15(sinRelative, cosPitch));
    put(0x46, multiplyQ15(cosRelative, cosPitch));
}

} // namespace scr

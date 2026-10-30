#include "physics/wheel_coordinates.h"

namespace scr {
namespace {

// Explicit two's-complement interpretation: no out-of-range unsigned-to-signed
// conversion. All intermediate arithmetic below fits in int32_t.
constexpr std::int32_t signedWord(std::uint16_t word) noexcept {
    const std::int32_t value = word;
    return value < 0x8000 ? value : value - 0x10000;
}

// 68000 ASR rounds down. C++17 division rounds toward zero, so correct a
// negative remainder instead of relying on implementation-defined signed >>.
// The only divisors used by this routine are the positive values 2 and 32.
constexpr std::int32_t divideRoundingDown(
    std::int32_t value, std::int32_t divisor) noexcept {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}

} // namespace

WheelCoordinates calculateWheelCoordinates(
    const WheelTransformWords& transform) noexcept {
    // Recovered from the supplied Amiga payload at 0x618ce..0x6194f (130 bytes).
    // Routine SHA-256:
    // 1a9ca0b143b8dcfe574f2cffc5242309c0f40acaf2b03f9cef12dff394ee33aa
    // All 29 instructions match the pinned reference; see
    // analysis/reference/vesuri/selected-comparison.json. This implements the
    // original disk routine, not the enhanced reference's timing changes.

    // Keep both halvings before subtraction: combining these divisions changes
    // negative rounding. Each half lies in [-16384, 16383], so their difference
    // fits a signed 16-bit word without wrapping.
    const auto lateralX = divideRoundingDown(
        divideRoundingDown(signedWord(transform.matrix3E), 2)
            - divideRoundingDown(signedWord(transform.matrix34), 2),
        32);
    const auto lateralY = divideRoundingDown(
        divideRoundingDown(signedWord(transform.matrix38), 2)
            - divideRoundingDown(signedWord(transform.matrix44), 2),
        32);
    const auto frontCenterX = divideRoundingDown(signedWord(transform.matrix46), 32);
    const auto frontCenterY = divideRoundingDown(signedWord(transform.matrix48), 32);

    // Centers and lateral offsets lie in [-1024, 1023]. Final values lie in
    // [-2048, 2047], so the original word ADD/SUB/NEG never overflow here and
    // these narrowing conversions are all exact.
    return {
        {static_cast<std::int16_t>(frontCenterX - lateralX),
         static_cast<std::int16_t>(frontCenterY - lateralY)},
        {static_cast<std::int16_t>(frontCenterX + lateralX),
         static_cast<std::int16_t>(frontCenterY + lateralY)},
        {static_cast<std::int16_t>(-frontCenterX),
         static_cast<std::int16_t>(-frontCenterY)},
    };
}

} // namespace scr

#include "physics/expected_wheel_heights.h"

namespace scr {
namespace {
constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value < 0x8000 ? std::int32_t(value) : std::int32_t(value) - 0x10000;
}

// Interpret the wrapped 32-bit intermediate as signed, then round down as
// ASR.L #8 does. Wide arithmetic avoids signed overflow and signed shifts.
constexpr std::int32_t reduceHeightScale(std::uint32_t bits) noexcept {
    const auto value = bits < 0x80000000u ? std::int64_t(bits)
                                        : std::int64_t(bits) - 0x100000000ll;
    return static_cast<std::int32_t>(value / 256 - (value % 256 < 0 ? 1 : 0));
}
} // namespace

ExpectedWheelHeights calculateExpectedWheelHeights(
    std::uint16_t pitch, std::uint16_t roll, std::uint32_t worldHeight,
    const TrigTable& table) noexcept {
    const auto pitchSine = gameSine(pitch, table);
    const auto pitchOffset = static_cast<std::uint32_t>(signedWord(pitchSine) * 16);
    const auto rollOffset = static_cast<std::uint32_t>(signedWord(gameSine(roll, table)) * 8);
    // Unsigned addition/subtraction preserves each original 32-bit wrap before
    // the final signed scale reduction, including inputs near the signed limit.
    const auto frontCenter = worldHeight + pitchOffset;
    return {reduceHeightScale(frontCenter + rollOffset),
            reduceHeightScale(frontCenter - rollOffset),
            reduceHeightScale(worldHeight - pitchOffset), pitchSine};
}

} // namespace scr

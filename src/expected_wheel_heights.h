#ifndef SCR_EXPECTED_WHEEL_HEIGHTS_H
#define SCR_EXPECTED_WHEEL_HEIGHTS_H

#include "transform_matrices.h"
#include <cstdint>

namespace scr {

struct ExpectedWheelHeights {
    std::int32_t frontLeft;
    std::int32_t frontRight;
    std::int32_t rearCenter;
    std::uint16_t pitchSine; // Also retained in the original shared scratch word.
};

// Original [0x61b70,0x61bcc): the body's expected wheel heights, to be compared
// with sampled road heights by the suspension stage. Inputs are original angle
// words and cameraWorldY's raw 32-bit fixed-point representation. Output scale
// has eight fewer fractional bits; physical units/axes remain provisional.
[[nodiscard]] ExpectedWheelHeights calculateExpectedWheelHeights(
    std::uint16_t pitch, std::uint16_t roll, std::uint32_t worldHeight,
    const TrigTable& table) noexcept;

} // namespace scr

#endif

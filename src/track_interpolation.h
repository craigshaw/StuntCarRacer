#ifndef SCR_TRACK_INTERPOLATION_H
#define SCR_TRACK_INTERPOLATION_H

#include <cstdint>

namespace scr {

// Original signed control-point words at RAM 0x1bc02/04/06/08 and unsigned
// fractions at 0x1bc4d (lateral) and 0x1bc41 (segment blend). Raw word bits
// preserve subtraction wraparound; all possible input patterns are supported.
struct TrackInterpolationInputs {
    std::uint16_t control1;
    std::uint16_t control2;
    std::uint16_t control3;
    std::uint16_t control4;
    std::uint8_t lateral;
    std::uint8_t blend;
};

// Reproduces D0 and the stored longword at 0x1bb18 from [0x5c554,0x5c5f2).
// The return is a signed 32-bit result represented as its unsigned bit pattern,
// with eight fractional bits relative to the control-point units. Preserve the
// original coarse-delta path and asymmetric rounding; this is not interchangeable
// with a conventional floating-point bilinear interpolation. CPU flags/registers
// other than the returned value, timing and scratch-memory placement are omitted.
[[nodiscard]] std::uint32_t interpolateTrackCoordinate(
    const TrackInterpolationInputs& inputs) noexcept;

} // namespace scr

#endif // SCR_TRACK_INTERPOLATION_H

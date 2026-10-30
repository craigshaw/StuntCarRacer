#ifndef SCR_WHEEL_COORDINATES_H
#define SCR_WHEEL_COORDINATES_H

#include <cstdint>

namespace scr {

// Raw 68000 words. The numeric suffixes preserve the reference disassembly's
// matrix field names. transform_matrices.cpp now produces these Q15 coefficients
// from the car/track angle words; physical coordinate conventions remain to map.
// The caller reads these words in big-endian order. The historical suffixes
// 44/46/48 correspond to actual matrix-block byte offsets 42/44/46.
struct WheelTransformWords {
    std::uint16_t matrix34; // RAM 0x1c264
    std::uint16_t matrix38; // RAM 0x1c268
    std::uint16_t matrix3E; // RAM 0x1c26e
    std::uint16_t matrix44; // RAM 0x1c272
    std::uint16_t matrix46; // RAM 0x1c274
    std::uint16_t matrix48; // RAM 0x1c276
};

struct WheelPoint {
    std::int16_t x;
    std::int16_t y;
};

// Point names and X/Y names follow the matched reference labels. They do not
// imply that these are screen pixels, world coordinates, or physical units.
struct WheelCoordinates {
    WheelPoint frontLeft;  // X at RAM 0x1bd02; Y at 0x1bd08
    WheelPoint frontRight; // X at RAM 0x1bd04; Y at 0x1bd0a
    WheelPoint rearCenter; // X at RAM 0x1bd06; Y at 0x1bd0c
};

// Reproduces the six stored output words of the original routine at 0x618ce.
// Every combination of input words is supported. CPU registers and condition
// codes are outside this native function's contract.
[[nodiscard]] WheelCoordinates calculateWheelCoordinates(
    const WheelTransformWords& transform) noexcept;

} // namespace scr

#endif // SCR_WHEEL_COORDINATES_H

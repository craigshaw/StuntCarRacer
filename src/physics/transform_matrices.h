#ifndef SCR_TRANSFORM_MATRICES_H
#define SCR_TRANSFORM_MATRICES_H

#include "physics/wheel_coordinates.h"
#include <array>
#include <cstdint>

namespace scr {

// Supply the original 258 big-endian table words, decoded to host integers.
// They are game data, extracted locally; no floating-point replacement is used.
// The final word is read by the original routine at the quadrant endpoint.
using TrigTable = std::array<std::uint16_t, 258>;

// A complete turn is 65536 units; subtraction wraps at 16 bits. X/Y/Z roles
// follow the reference's cameraAngleX/Y/Z fields; physical axes remain to map.
struct TransformAngles {
    std::uint16_t pitch;        // RAM 0x1bce4
    std::uint16_t yaw;          // RAM 0x1bce6
    std::uint16_t roll;         // RAM 0x1bce8
    std::uint16_t trackHeading; // RAM 0x1bd5a (segmentTargetAngle)
};

// Compatibility layout for the original coefficient block at RAM 0x1c230.
// This is a collection of intermediate products and transform coefficients,
// not a single 3x3 matrix. updateTransformMatrices writes 30 words and preserves
// six unused slots at byte offsets 0x26,0x32,0x36,0x3a,0x3c,0x40.
struct TransformMatrices {
    std::array<std::uint16_t, 36> words{};
    [[nodiscard]] WheelTransformWords wheelInputs() const noexcept;
};

// Returns the original signed Q15 result encoded as a 16-bit word.
[[nodiscard]] std::uint16_t gameSine(std::uint16_t angle, const TrigTable& table) noexcept;
[[nodiscard]] std::uint16_t gameCosine(std::uint16_t angle, const TrigTable& table) noexcept;

// Reproduces final coefficient memory for original routine [0x61368,0x6158c).
// CPU registers, flags, stack temporaries and execution timing are not outputs.
void updateTransformMatrices(const TransformAngles& angles, const TrigTable& table,
                             TransformMatrices& matrices) noexcept;

} // namespace scr

#endif

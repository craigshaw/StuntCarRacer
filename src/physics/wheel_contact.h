#ifndef SCR_WHEEL_CONTACT_H
#define SCR_WHEEL_CONTACT_H

#include <cstdint>

namespace scr {

struct WheelContactInputs {
    std::uint16_t lateralPosition; // Original word at 0x1bc22, including wrap.
    std::uint8_t reverseDirection;
    std::uint8_t speedHighByte; // Byte at 0x1bd5c, tested as signed.
    std::uint8_t pitchHighByte; // Byte at 0x1bce4, tested as signed.
};

struct WheelContactState {
    std::uint32_t height; // Selected wheel's previous/new height bit pattern.
    std::uint32_t sampledHeight; // Post-edge result in original shared scratch.
    std::uint8_t outOfBoundsFlag;
    std::uint8_t renderingOrderMode;
    std::uint8_t bouncePhase;
};

// Apply [0x5c5f8,0x5c65a) and its edge helper [0x5c808,0x5c890)
// after interpolateTrackCoordinate has produced the raw height. In the game
// this runs successively for the three wheel heights; carry the shared flags
// and bouncePhase between calls. The old sampledHeight is overwritten.
// All integer bit patterns are supported, including signed-byte and word-wrap
// corner cases. CPU registers/flags, wheel-array indexing and timing are omitted.
void applyWheelHeightContact(std::uint32_t rawInterpolatedHeight,
                            const WheelContactInputs& inputs,
                            WheelContactState& state) noexcept;

} // namespace scr

#endif // SCR_WHEEL_CONTACT_H

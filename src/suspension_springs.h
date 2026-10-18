#ifndef SCR_SUSPENSION_SPRINGS_H
#define SCR_SUSPENSION_SPRINGS_H

#include <array>
#include <cstdint>

namespace scr {

using SuspensionAttenuationTable = std::array<std::uint8_t, 128>;

struct SuspensionSpringInputs {
    std::uint32_t frontLeftTravel;  // 0x1bcb0, raw signed-longword encoding
    std::uint32_t frontRightTravel; // 0x1bcb4
    std::uint32_t rearTravel;       // 0x1bcb8
    std::uint16_t averageVelocity;  // 0x1bd38, raw signed-word encoding
};

// Original byte writes address the high byte of these big-endian words.
struct SpringComponent {
    std::uint8_t value{};
    std::uint8_t retainedLowByte{};
};

struct SuspensionSpringScratch {
    std::uint8_t multiplier{};       // tempByte3, 0x1bb1a
    std::uint8_t productLow{};       // tempByte4, 0x1bb1b
    std::uint8_t rawDisplacement{};  // 0x1bb2b
    std::uint8_t savedAttenuation{}; // 0x1bb2c
    std::uint8_t attenuation{};      // 0x1bb2d
    std::uint8_t signByte{};         // trackSideIndicatorCopy, 0x1bbbb
};

struct SuspensionSpringState {
    std::uint16_t rollAngle{};          // 0x1bd40
    std::uint16_t pitchAngle{};         // 0x1bd42
    std::uint16_t yawAngle{};           // 0x1bd44
    std::uint16_t retainedWord46{};     // 0x1bd46, not written
    std::uint16_t rollDisplacement{};   // 0x1bd48
    std::uint16_t pitchReferenceZero{}; // 0x1bd4a
    std::uint16_t pitchDisplacement{};  // 0x1bd4c
    SpringComponent rollComponent;     // 0x1bd4e, low byte retained
    SpringComponent pitchComponent;    // 0x1bd50, low byte retained
    SpringComponent yawComponent;      // 0x1bd52, low byte retained
    SuspensionSpringScratch scratch;
};

// Reproduces the memory outputs of [0x622dc,0x62424) and its two arithmetic
// helpers. Requires the normal game's entry condition D1 bits 8..15 == 0;
// the original attenuation helper otherwise indexes outside its 128-byte table.
// CPU flags/registers/stack timing are not native outputs. All travel and
// velocity bit patterns are supported; additions/subtractions wrap explicitly.
void calculateSuspensionSpringForces(const SuspensionSpringInputs& input,
                                    const SuspensionAttenuationTable& table,
                                    SuspensionSpringState& state) noexcept;

} // namespace scr
#endif

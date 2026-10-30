#pragma once
#include <array>
#include <cstdint>

namespace scr {
// The twelve hardware sprites are extracted from the user's disk by startup.
// Slots correspond to graphics IDs 37..48; the images stay outside source code.
struct WheelAnimationData {
    std::array<std::uint32_t,12> spritePointers{};
};
struct WheelAnimationInputs {
    std::uint8_t displayUpdate{}, disabled{}, speedHigh{};
    std::uint16_t localForwardVelocity{};
    // Right then left, matching the original sprite/DMA channel order.
    std::array<std::uint16_t,2> height{};
};
struct WheelAnimationState {
    std::uint8_t accumulator{}, phase{};
    std::array<std::uint32_t,2> spritePointers{};
    std::uint8_t framesToWait{}, framesToWaitWhenFading{};
};
struct WheelAnimationSelection {
    bool updated{};
    std::array<std::uint8_t,2> spriteIds{};
};
// Original advanceWheelRotationFrame [0x5e744,0x5e778). A zero velocity
// selects the forward order; reversing changes the order without resetting it.
void advanceWheelRotationFrame(std::uint16_t localForwardVelocity,
    WheelAnimationState&);
// Original updateWheelGraphics [0x651b8,0x6521c). The speed is the HIGH
// byte of wheelSpeed at 0x1bc62; each byte-add carry advances one of 3 phases.
// Valid initialized phases are 0..2. Heights use a signed comparison with 126.
[[nodiscard]] WheelAnimationSelection updateWheelGraphics(
    const WheelAnimationData&,const WheelAnimationInputs&,WheelAnimationState&);
// Original PAL VBlank prefix [0x616dc,0x6171a), before the audio-only tail.
// Call once per 20 ms independently of the 120 ms simulation and 60 Hz camera.
// The original display/disable bytes gate wheel updates, not its wait counters.
[[nodiscard]] WheelAnimationSelection updateWheelVblank(
    const WheelAnimationData&,const WheelAnimationInputs&,WheelAnimationState&);
} // namespace scr

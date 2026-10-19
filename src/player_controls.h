#pragma once
#include <array>
#include <cstdint>

namespace scr {
using ControlKeyMap = std::array<std::uint8_t,5>;
using KeyboardState = std::array<std::uint8_t,128>; // Original pressed marker is 0xb3.
struct ControllerHardware {
    std::uint16_t joy1dat{};
    std::uint8_t ciaPortA{0xff}, ciaDataDirection{0xff};
};
struct PlayerControlsInputs {
    KeyboardState keyboard{};
    ControllerHardware hardware{};
    std::uint16_t localVelocityZ{};
    std::uint8_t wheelMovementActive{}, raceStartTimer{}, postWreckStateFlag{}, displayUpdateFlag{};
    std::uint8_t gasInputIntensity{}, gasInputAccumulator{}, fuelConsumptionRate{}, frameThrottleFlag{}, maxBoostFuel{};
};
struct PlayerControlsState {
    std::uint8_t inputFlags{}, joystickState{}, cipherIndex{};
    std::uint32_t randomSeed{}; // Shared with countdown: original0x625a2..a5.
    std::uint8_t randomTail{};  // Original0x625a6.
    std::uint8_t steeringDirection{}, raceActive{}, accelerationState{}, boostActive{}, boostFuel{}, fuelTimer{};
    std::uint16_t gasOutput{}; // Accumulator high byte / intensity low byte.
};
struct PlayerControlsEffects {
    bool joystickRead{};
    std::uint8_t ciaDataDirection{}; // When joystickRead: original DDRA & 0x7f.
    bool fuelHudRedraw{};
    std::uint8_t displayedFuel{}; // Packed byte supplied to the original HUD.
};

// Original BCD update [0x60824,0x60846), before its bitmap-rendering suffix.
// Supports every byte encoding, including the original non-BCD edge behavior.
std::uint8_t adjustBoostFuel(std::uint8_t delta, std::uint8_t fuel, std::uint8_t maximum) noexcept;

// Local/practice path of processPlayerInput, including keyboard/joystick
// decoding, RNG, latched acceleration and boost. Keys must be below128.
// The network transport branch is outside this API; it is never silently used.
// Graphics are represented by a fuel-HUD redraw event; no bitmap is rendered.
// Original byte-index convention requires D1/D2 bits8..15 zero on entry.
[[nodiscard]] PlayerControlsEffects processLocalPlayerInput(
    const PlayerControlsInputs& input, const ControlKeyMap& keys, PlayerControlsState& state);
} // namespace scr

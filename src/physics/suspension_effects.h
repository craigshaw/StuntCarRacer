#ifndef SCR_SUSPENSION_EFFECTS_H
#define SCR_SUSPENSION_EFFECTS_H
#include <array>
#include <cstdint>

namespace scr {

struct SuspensionEffectsInputs {
    std::uint16_t carHeight{},trackSurfaceHeight{},lateralOffset{};
    std::uint8_t frameThrottle{},networkMode{},multiplayerSync{},playerSegment{},opponentSegment{};
    std::uint8_t selectedTrack{},raceActive{},hardImpactCount{};
    std::uint16_t maxCompressionVelocity{};
};

struct RaceStartState {
    std::uint8_t timer{},complete{},trackSide{};
    std::uint16_t roll{},carRollAngle{},targetPitchRate{};
    std::uint8_t crashRecoveryTimer{},offTrackFlags{},messageActive{},messageIndex{},messageMode{};
    std::uint32_t randomSeed{};
    std::uint8_t randomTail{};
};

struct OpponentCollisionState {
    std::uint8_t active{},cooldown{};
    std::uint16_t opponentSpeed{};
    std::array<std::uint16_t,3> wheelVelocities{};
    std::array<std::uint16_t,3> forces{}; // lateral, vertical, forward
};

struct SuspensionEffectsState {
    RaceStartState race;
    OpponentCollisionState collision;
    std::array<std::uint16_t,3> springAngles{}; // roll, pitch, yaw
    std::uint16_t savedYawSpring{};
    std::uint8_t impactCooldown{},impactVolume{};
};

struct SuspensionSampleRequests {
    std::array<std::uint8_t,2> ids{};
    std::uint8_t count{};
};

// Complete countdown, including multiplayer side selection, lift/roll motion,
// original deterministic random seed and message state. Original [5b32e,5b472)
// plus helpers. The original roll helper requires D2 bits8..15 zero.
void updateRaceStartCountdown(const SuspensionEffectsInputs& inputs,SuspensionEffectsState& state) noexcept;

// Complete collision response [63e2e,63ede). Returns whether sample2 is requested.
// The caller must service the request through the recovered audio playback code.
[[nodiscard]] bool applyOpponentCollisionForces(SuspensionEffectsState& state) noexcept;

// Remaining original suspension body [62042,620b6), after compression/damping.
// Requests are in original call order (collision2, then impact3). Service them
// with the recovered audio code using impactVolume as sample3's volume LOW byte.
// Deferring those requests until this function returns preserves their data:
// collision does not read sample3's volume, and audio does not affect this state.
[[nodiscard]] SuspensionSampleRequests runSuspensionEffects(
    const SuspensionEffectsInputs& inputs,SuspensionEffectsState& state) noexcept;

} // namespace scr
#endif

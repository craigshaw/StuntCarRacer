#pragma once
#include <array>
#include <cstdint>

namespace scr {
// Values also form the replay trace's outer_action column.
enum class RaceTransitionAction : std::uint8_t { continueTick=0,restartRace=1,raceEnd=2,quit=3,pauseWait=4 };
enum class RaceTransitionEventKind : std::uint8_t {
    renderPlayerIcon,renderMessagePanel,displayEndMessage,renderExistingHole,
    disableAudio,updateNetworkFlags,waitForFire,waitForUnpause,resumeEngine,waitForFrame,fadeToMenu
};
struct RaceTransitionEvent { RaceTransitionEventKind kind{};std::uint8_t value{}; };
struct RaceTransitionEffects {
    RaceTransitionAction action{};
    std::array<RaceTransitionEvent,10> events{};
    unsigned count{};
    std::uint8_t restartSegment{};
    std::uint16_t fadeColor{};
};
struct RaceTransitionInputs {
    std::uint8_t raceStartTimer{},wheelMovementActive{},offTrackFlags{},carHeightLow{};
    std::uint8_t lastValidSegment{},raceComplete{},lapDisplayDuration{},majorImpactCooldown{},holePosition{};
    // Local keyboard queries return equality to 0xb3. These are the two
    // relevant query results, sampled at their original call boundaries.
    bool pausePressed{},quitPressed{};
    std::uint8_t selectedTrackInDivision{},savedHolePosition{},randomTail{};
    std::uint16_t menuFadeColor{};
    std::uint32_t randomSeed{};
};
struct RaceTransitionState {
    std::uint8_t frameBufferSyncMask{},frameBufferToggle{},restartCountdown{},blinkCountdown{};
    std::uint8_t raceCompletionCheckFlag{},outcomeFlags{},gameExitFlag{},networkInputSyncEnabled{};
    std::uint8_t pauseState{},disableWheelUpdateFlag{},messageActive{},messageMode{},messageIndex{},resultsIcon{};
    // Native counterpart of three message bytes saved on the original stack
    // while waitForUnpause is suspended. These are not additional game RAM.
    std::uint8_t savedMessageActive{},savedMessageMode{},savedMessageIndex{};
    std::uint8_t bufferSelector{},framesToWait{},bufferToggleMirror{},copperUpdatePending{};
    std::uint8_t displayUpdateFlag{},holePosition{};
    std::uint16_t enginePitchDelta{},wheelSpeed{},dmaControl{};
    std::uint32_t frameBuffers{},displayFrameBuffer{},renderFrameBuffer{},renderFrameBufferInset{},pendingDisplayFrameBuffer{};
    std::uint32_t savedRandomSeed{};
};
// Original local-mode end gate [0x5d4ba,0x5d574). On raceEnd the caller must
// swap buffers, then finishRaceEnd. Render/audio/input operations are events.
RaceTransitionEffects inspectRaceEnd(const RaceTransitionInputs&,RaceTransitionState&) noexcept;
// Original 0x5dac0 plus 0x1b7b6. Returns false at the platform wait boundary
// without changing state. The caller supplies explicit vblank/frame progress.
bool swapRaceBuffers(RaceTransitionState&) noexcept;
RaceTransitionEffects finishRaceEnd(RaceTransitionState&) noexcept;
// Common original exit [0x5d674,0x5d724), Practice (selectedTrack sign clear).
// The fade is an explicit presentation event; seed-save byte overlap is kept.
RaceTransitionEffects finishLocalRaceExit(const RaceTransitionInputs&,RaceTransitionState&);
// Original [0x5d5ac,0x5d674), after buffer swap and pause have completed.
RaceTransitionEffects continueRaceTransition(const RaceTransitionInputs&,RaceTransitionState&) noexcept;
// Original 0x5d9dc..0x5daac. First press arms pause for the following tick;
// the next call enters the wait. Message/pixels are a presentation boundary.
RaceTransitionEffects beginRacePause(const RaceTransitionInputs&,RaceTransitionState&) noexcept;
RaceTransitionEffects resumeRacePause(RaceTransitionState&) noexcept;
} // namespace scr

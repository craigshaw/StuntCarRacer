#pragma once
#include <array>
#include <cstdint>

namespace scr {
struct PackedLapTime {
    std::uint8_t minutes{},seconds{},subseconds{};
};
struct RaceTimingInputs {
    std::array<std::uint8_t,2> segmentIndex{};
    std::array<std::uint16_t,2> trackProgress{};
    std::array<std::uint8_t,256> playerNames{};
    std::uint16_t playerDistanceDifference{};
    std::uint8_t selectedTrackInDivision{},offTrackStateFlags{},finishLineSegment{},lapLineSegment{},numberOfTrackSegments{};
    std::uint8_t maximumLaps{},currentPlayerId{},raceStartTimer{},wheelMovementActive{},raceCompletionCheckFlag{};
};
struct RaceTimingState {
    std::array<PackedLapTime,24> times{};
    std::array<std::uint8_t,2> crossingArmed{},lapCounter{};
    PackedLapTime networkTimeBuffer{};
    std::array<std::uint8_t,12> lapRecordName{},raceRecordName{};
    std::uint8_t globalFrameCounter{},throttleAccumulator{},throttleFlag{},lapDisplayDuration{};
    std::uint8_t blinkCountdown{},blinkFlag{},frameBufferSyncMask{};
    std::uint8_t raceComplete{},winnerBits{},outcomeFlags{},displayFlags{},bestLapHolder{},bestRaceHolder{};
    std::uint8_t messageActive{},messageMode{},messageIndex{};
    // These aliases are shared with physics arithmetic and rendering.
    std::uint8_t multiplierScratch{},productLowScratch{},savedPlayerScratch{};
    std::uint8_t resultsIcon{},leaderIcon{},renderingIndex{},subsecondVisibility{};
};
enum class RaceTimingEventKind : std::uint8_t { lapTimerHud,resultsHud,lapCounterHud,leaderIcon };
struct RaceTimingEvent { RaceTimingEventKind kind{};std::uint8_t value{}; };
struct RaceTimingEffects {
    std::array<RaceTimingEvent,12> events{};
    unsigned count{};
};
// Original local-mode scheduler [0x5db34,0x5dc38), including lap crossing,
// records and finish state. Graphics become ordered events. Network transport
// is outside this API. currentPlayerId must be 0..11 (24 record slots).
RaceTimingEffects updateRaceTiming(const RaceTimingInputs&,RaceTimingState&);
// Useful standalone recovered stages; player must be 0 or 1.
void updateBlinkCountdown(const RaceTimingInputs&,RaceTimingState&) noexcept;
RaceTimingEffects incrementLapClock(std::uint8_t delta,unsigned player,RaceTimingState&);
} // namespace scr

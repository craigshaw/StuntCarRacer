#pragma once
#include "race/crossing_timing.h"
#include "race/race_timing.h"
#include <optional>

namespace scr::runtime {
constexpr std::uint32_t enhancedTimingMethodVersion=1;
constexpr std::int64_t recoveryPenaltyMicroseconds=260000;
struct LapTimingBoundary {
    std::int64_t observedMicroseconds{},crossingMicroseconds{};
    std::uint64_t recoveries{},recoveryEpoch{};
    unsigned player{};
    timing::Reason confidence{timing::Reason::missingPrevious};
};
struct EnhancedTime {
    std::uint32_t methodVersion{enhancedTimingMethodVersion};
    std::int64_t elapsedMicroseconds{},penaltyMicroseconds{},scoredMicroseconds{};
    std::uint64_t recoveries{};
    timing::Reason confidence{timing::Reason::missingPrevious};
    bool hasEstimate{};
    bool rankable()const noexcept{return hasEstimate&&confidence==timing::Reason::reliable;}
    std::int64_t milliseconds()const noexcept{return scoredMicroseconds/1000+(scoredMicroseconds%1000>=500?1:0);}
};
EnhancedTime measureEnhancedTime(const LapTimingBoundary& start,const LapTimingBoundary& finish)noexcept;
struct TimedRaceResult {PackedLapTime original{};EnhancedTime enhanced;};
struct EnhancedLapTiming {
    LapTimingBoundary start,finish;
    EnhancedTime lap;
    std::optional<TimedRaceResult> race;
};
struct LapTimingCompletion {
    std::uint8_t counter{};
    PackedLapTime original{};
    EnhancedLapTiming enhanced;
};
struct LapTimingStatistics {
    std::uint64_t movements{},timingUpdates{},crossings{},completions{},rankableLaps{},races{},recoveries{},recoveryObservations{};
};
// Observes native stages independently of recording buffers, PBs or rendering.
// observeTiming returns each full lap once; an attached partial lap is skipped.
class LapTiming {
public:
    LapTiming(timing::Gate gate,bool competitive,unsigned initialRestarts=0);
    void observeMovement(timing::Observation);
    std::optional<LapTimingCompletion> observeTiming(const std::optional<HumanLapCrossing>&,
        std::optional<PackedLapTime> originalTotal={});
    void stop()noexcept;
    const LapTimingStatistics& statistics()const noexcept{return statistics_;}
private:
    timing::Gate gate_;
    bool competitive_{},stopped_{};
    unsigned restarts_{};
    std::uint64_t recoveries_{},recoveryEpoch_{},movementSerial_{},timingSerial_{};
    std::optional<timing::Observation> previous_,current_;
    std::optional<LapTimingBoundary> start_,raceStart_;
    std::uint8_t lastCounter_{};
    unsigned raceLaps_{};
    timing::Reason raceConfidence_{timing::Reason::reliable};
    LapTimingStatistics statistics_;
    LapTimingBoundary boundary()const;
};
} // namespace scr::runtime

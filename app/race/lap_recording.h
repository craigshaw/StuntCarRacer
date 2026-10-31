#pragma once
#include "input/drive_input.h"
#include "presentation/ghost_bridge.h"
#include "presentation/presentation_interpolation.h"
#include "race/race_timing.h"
#include "render/ghost_car.h"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace scr::runtime {
enum class GhostLapSource : std::uint8_t {Practice,League};
struct GhostLapKey {
    std::string career;
    unsigned track{};
    bool superLeague{};
    bool operator<(const GhostLapKey&)const noexcept;
};
struct GhostLapContext {GhostLapKey key;GhostLapSource source{};};
constexpr unsigned maxGhostLapSamples=65536,maxGhostLapBridgeShapes=64;
constexpr std::uint16_t noGhostBridge=0xffff;
struct GhostLapSample {
    std::int64_t microseconds{};
    RenderCameraInputs pose;
    std::uint16_t bridge{noGhostBridge};
    GhostBridgeRegion region{};
    std::uint8_t segment{};
    bool cut{};
};
struct GhostLapBoundary {
    std::int64_t observedMicroseconds{},alignedMicroseconds{};
    std::uint16_t progress{};
    std::uint8_t correctionHundredths{};
    bool interpolated{},recovery{};
};
struct GhostLap {
    GhostLapContext context;
    PackedLapTime completedTime;
    unsigned hundredths{},controlPresets{};
    std::int64_t durationMicroseconds{};
    GhostLapBoundary start,finish;
    std::vector<GhostLapSample> samples;
    std::vector<GhostBridgeShape> bridges;
};
// Ranking always uses original packed-decimal time, never movement duration.
unsigned ghostLapHundredths(PackedLapTime);
class PersonalBestLaps {
public:
    std::shared_ptr<const GhostLap> best(const GhostLapKey&)const;
    std::size_t size()const noexcept{return bests_.size();}
    void forgetCareer(const std::string&);
private:
    friend class LapRecorder;
    bool accept(GhostLap);
    std::map<GhostLapKey,std::shared_ptr<const GhostLap>> bests_;
};
struct LapMovementObservation {
    std::int64_t microseconds{};
    RenderCameraInputs pose;
    unsigned segment{},segmentLength{},lapSegment{},segmentCount{},progress{},restarts{};
    bool grounded{},recovery{};
    DriveControlPreset preset{};
    std::optional<GhostBridgeShape> bridge;
};
struct GhostLapPlayback {
    GhostCarPose pose;
    double opacity{1};
    bool conditionsDiffer{},finished{};
    std::int64_t elapsedMicroseconds{};
    std::shared_ptr<const GhostLap> target;
};
struct LapRecordingStatistics {
    unsigned movements{},timingUpdates{},recoveryTimingUpdates{},starts{},completions{},recordedLaps{},improvements{},overflowCandidates{};
};
// observe* runs only at native stage boundaries. All playback queries are pure.
// A new recorder waits for an observed crossing; a partial entry lap is skipped.
class LapRecorder {
public:
    LapRecorder(PersonalBestLaps&,GhostLapContext,const TrigTable&);
    void observeMovement(LapMovementObservation);
    void observeTiming(const RaceTimingEffects&);
    void stop();
    std::optional<GhostLapPlayback> playback(std::int64_t absoluteMicroseconds,bool interpolate=true)const;
    const LapRecordingStatistics& statistics()const noexcept{return statistics_;}
    const GhostLapContext& context()const noexcept{return context_;}
private:
    struct Epoch {
        std::int64_t start{};
        std::shared_ptr<const GhostLap> target;
        GhostBridgeVisibility visibility;
        std::optional<std::int64_t> lastCut;
    };
    PersonalBestLaps& bests_;
    GhostLapContext context_;
    const TrigTable& trig_;
    std::optional<LapMovementObservation> previous_,current_;
    std::optional<GhostLap> candidate_;
    std::deque<Epoch> epochs_;
    GhostBridgeRegionTracker region_;
    LapRecordingStatistics statistics_;
    bool stopped_{},overflowed_{};
    GhostLapBoundary boundary(bool first)const;
    void append(const LapMovementObservation&,bool cut);
    void compatibility();
};
} // namespace scr::runtime

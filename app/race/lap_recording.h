#pragma once
#include "input/drive_input.h"
#include "race/ghost_lap_data.h"
#include "race/practice_ghost_target.h"
#include "presentation/ghost_bridge.h"
#include "presentation/presentation_interpolation.h"
#include "race/race_timing.h"
#include "render/ghost_car.h"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <string>

namespace scr::runtime {
enum class GhostLapSource : std::uint8_t {Practice,League};
struct GhostLapKey {
    std::string career;
    unsigned track{};
    bool superLeague{};
    bool operator<(const GhostLapKey&)const noexcept;
};
struct GhostLapContext {GhostLapKey key;GhostLapSource source{};std::uint64_t trackContentIdentity{};};
std::uint64_t ghostTrackContentIdentity(const std::vector<std::uint8_t>& payload);
struct GhostLap : LapRecording {
    GhostLapContext context;
    unsigned hundredths{},controlPresets{};
};
struct LapCompletion {
    std::uint8_t counter{};
    PackedLapTime time{};
    std::unique_ptr<LapRecording> recording;
};
using LapCompletionSink=std::function<void(LapCompletion)>;
// Ranking always uses original packed-decimal time, never movement duration.
unsigned ghostLapHundredths(PackedLapTime);
class PersonalBestLaps {
public:
    std::shared_ptr<const GhostLap> best(const GhostLapKey&)const;
    std::size_t size()const noexcept{return bests_.size();}
    void forgetCareer(const std::string&);
    void forget(const GhostLapKey& key){bests_.erase(key);}
    bool restore(GhostLap lap){return accept(std::move(lap));}
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
    std::shared_ptr<const LapRecording> target;
    std::shared_ptr<const PinnedHallGhost> hallTarget;
};
struct LapRecordingStatistics {
    unsigned movements{},timingUpdates{},recoveryTimingUpdates{},starts{},completions{},recordedLaps{},improvements{},overflowCandidates{};
};
// observe* runs only at native stage boundaries. All playback queries are pure.
// A new recorder waits for an observed crossing; a partial entry lap is skipped.
class LapRecorder {
public:
    LapRecorder(PersonalBestLaps&,GhostLapContext,const TrigTable&,std::shared_ptr<const PinnedHallGhost> target={});
    LapRecorder(unsigned track,const TrigTable&);
    void observeMovement(LapMovementObservation);
    void observeTiming(const RaceTimingEffects&,const LapCompletionSink& sink={});
    void stop();
    void clear();
    std::optional<GhostLapPlayback> playback(std::int64_t absoluteMicroseconds,bool interpolate=true)const;
    const LapRecordingStatistics& statistics()const noexcept{return statistics_;}
    const GhostLapContext& context()const noexcept{return context_;}
private:
    struct Epoch {
        std::int64_t start{};
        std::shared_ptr<const LapRecording> target;
        GhostBridgeVisibility visibility;
        std::optional<std::int64_t> lastCut;
    };
    PersonalBestLaps* bests_{};
    GhostLapContext context_;
    std::shared_ptr<const PinnedHallGhost> hallTarget_;
    const TrigTable& trig_;
    std::optional<LapMovementObservation> previous_,current_;
    std::optional<LapRecording> candidate_;
    std::deque<Epoch> epochs_;
    GhostBridgeRegionTracker region_;
    LapRecordingStatistics statistics_;
    bool stopped_{},overflowed_{},observedStart_{},pbEligible_{true};
    std::uint8_t lastCounter_{};
    unsigned candidatePresets_{};
    GhostLapBoundary boundary(bool first)const;
    void append(const LapMovementObservation&,bool cut);
    void compatibility();
};
} // namespace scr::runtime

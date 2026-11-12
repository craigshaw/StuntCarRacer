#pragma once
#include "race/lap_timing.h"
#include "presentation/native_cockpit_presentation.h"
#include <vector>

namespace scr::runtime {
struct EnhancedClockState {
    std::optional<LapTimingBoundary> start;
    LapTimingBoundary movement;
    std::optional<EnhancedTime> held,bestLap,bestRace,raceResult;
    EnhancedClockValue frozenValue;
    bool holding{},frozen{};
};
struct EnhancedClockKey {std::int64_t microseconds{};EnhancedClockState state;};
struct EnhancedClockSnapshot {EnhancedClockState before;std::vector<EnhancedClockKey> changes;};
// Immutable frame-local keys include backdated crossings and future recovery
// observations. Queries apply only keys reached by the presented car's clock.
EnhancedClockDisplay queryEnhancedClock(const EnhancedClockSnapshot&,const EnhancedClockSnapshot* next,std::int64_t microseconds);
std::string formatEnhancedClock(EnhancedClockValue);
void composeEnhancedClock(CockpitInstrumentPixels&,const std::array<std::uint8_t,96*8>& font,const EnhancedClockDisplay&);
void composeEnhancedFinish(std::vector<std::uint32_t>&,const std::array<std::uint8_t,96*8>& font,const std::array<std::uint32_t,16>& palette,const EnhancedClockDisplay&);
class EnhancedClockPresentation {
public:
    void beginFrame();
    void observeMovement(LapTimingBoundary);
    void observeTiming(LapTimingBoundary,const LapTimingBoundary* crossing,const LapTimingCompletion*,bool holding,bool finished);
    void records(std::optional<EnhancedTime> lap,std::optional<EnhancedTime> race);
    EnhancedClockSnapshot snapshot()const{return {before_,changes_};}
    EnhancedClockSnapshot seed()const{return {state_,{}};}
private:
    EnhancedClockState state_,before_;
    std::vector<EnhancedClockKey> changes_;
    void key(std::int64_t);
};
} // namespace scr::runtime

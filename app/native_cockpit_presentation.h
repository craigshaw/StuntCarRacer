#pragma once
#include "native_cockpit_instruments.h"
#include "state/native_race.h"
#include <array>
#include <memory>
#include <optional>

namespace scr::viewer {
constexpr unsigned cockpitInstrumentTop=168;
using CockpitInstrumentPixels=std::array<std::uint8_t,320*(200-cockpitInstrumentTop)>;
// Independent renderer memory retains the original two buffers and text state.
// Only immutable snapshots and renderer-owned startup bytes cross this boundary.
class NativeCockpitPresentation {
public:
    explicit NativeCockpitPresentation(const NativeMemory&,bool fresh=false);
    void update(state::replay::ReplayPresentationStage,const NativeMemory&,const RaceTimingEffects&);
    // The player can reveal the running clock's reserved hundredths digits.
    // Default capture retains the original event-driven HUD for comparisons.
    CockpitInstrumentPixels capture(bool runningHundredths=false)const;
    void storeInitialState(NativeMemory&)const;
private:
    std::unique_ptr<NativeMemory> memory_;
    std::optional<PackedLapTime> lapTime_;
    bool finished_{};
};
}

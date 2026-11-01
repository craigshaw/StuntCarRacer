#pragma once
#include "race/lap_recording.h"
#include "persistence/shared_hall_store.h"
#include "state/human_record_adapter.h"

namespace scr::runtime {
// Host-only delivery state, never part of a native checkpoint or manual save.
class HallRaceRecording {
public:
    HallRaceRecording(HallAttemptId run,HallBoardKey,HallCatalog,bool competitive);
    void complete(LapCompletion,const state::HumanRaceRecord&);
    std::vector<HallSubmission> take();
private:
    HallAttemptId run_;
    HallBoardKey board_;
    HallCatalog catalog_;
    bool competitive_,raceEligible_{true};
    unsigned fullLaps_{};
    std::uint64_t crossing_{};
    std::optional<unsigned> player_;
    std::vector<HallSubmission> completed_;
};
} // namespace scr::runtime

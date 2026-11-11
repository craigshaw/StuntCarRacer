#pragma once
#include "race/crossing_timing.h"
#include "state/track_sampling_adapter.h"

namespace scr::state {
// Observe after native progress processing at the wheels boundary. The caller
// supplies the outer-step clock and recovery identity; no RAM is modified.
inline scr::timing::Observation readHumanTimingObservation(const memory::ChipRam& ram,
    std::int64_t microseconds,unsigned restarts,bool found,bool recovery){
    scr::timing::Observation result;result.us=microseconds;result.restarts=restarts;
    result.found=found;result.recovery=recovery;
    for(unsigned axis=0;axis<3;++axis)result.position[axis]=memory::readLong(ram,0x1bcd8+4*axis);
    result.segment=ram[0x1bb1c];result.progress=memory::readWord(ram,0x1bb0a);
    result.offTrack=ram[0x1bb9c];result.player=ram[0x5eb79];return result;
}
} // namespace scr::state

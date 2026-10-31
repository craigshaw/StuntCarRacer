#pragma once
#include "race/lap_recording.h"
#include "state/native_checkpoint_io.h"
#include <stdexcept>

namespace scr::runtime {
constexpr unsigned ghostLapFormatVersion=1,ghostLapRulesVersion=1;
class UnsupportedGhostLap : public std::runtime_error {public:using std::runtime_error::runtime_error;};
state::checkpoint::Bytes encodeGhostLap(const GhostLap&);
GhostLap decodeGhostLap(const state::checkpoint::Bytes&);
GhostLap decodeGhostLap(const state::checkpoint::Bytes&,const GhostLapKey&,std::uint64_t trackContentIdentity);
} // namespace scr::runtime

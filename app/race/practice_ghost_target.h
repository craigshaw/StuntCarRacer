#pragma once
#include "persistence/hall_ghost_codec.h"
#include <memory>

namespace scr::runtime {
// A decoded Hall rival owns its movement independently of the table and PBs.
struct PinnedHallGhost {
    HallAttempt attempt;
    HallCatalog catalog;
    LapRecording recording;
};
std::shared_ptr<const PinnedHallGhost> pinHallGhost(const HallAttempt&,const HallGhost&,HallCatalog);
} // namespace scr::runtime

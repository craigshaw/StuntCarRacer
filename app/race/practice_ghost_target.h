#pragma once
#include "persistence/hall_ghost_codec.h"
#include "persistence/enhanced_record_codec.h"
#include <memory>

namespace scr::runtime {
// A decoded Hall rival owns its movement independently of the table and PBs.
struct PinnedHallGhost {
    HallAttempt attempt;
    HallCatalog catalog;
    LapRecording recording;
};
std::shared_ptr<const PinnedHallGhost> pinHallGhost(const HallAttempt&,const HallGhost&,HallCatalog);
struct PinnedEnhancedHallGhost {
    EnhancedHallAttempt attempt;
    EnhancedCatalog catalog;
    EnhancedRecording recording;
};
std::shared_ptr<const PinnedEnhancedHallGhost> pinEnhancedHallGhost(const EnhancedHallAttempt&,const EnhancedHallGhost&,EnhancedCatalog);
} // namespace scr::runtime

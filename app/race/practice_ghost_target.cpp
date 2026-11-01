#include "race/practice_ghost_target.h"

namespace scr::runtime {
std::shared_ptr<const PinnedHallGhost> pinHallGhost(const HallAttempt& attempt,const HallGhost& ghost,HallCatalog catalog){
    requireHallGhostBinding(ghost,attempt,catalog);
    // This boundary also rejects malformed in-memory callers. Resolution runs
    // on the Hall worker, never on the driving or menu presentation thread.
    (void)encodeHallGhost(ghost);
    auto target=std::make_shared<PinnedHallGhost>();target->attempt=attempt;target->catalog=catalog;
    auto& lap=target->recording;lap.completedTime=ghost.time;lap.durationMicroseconds=ghost.durationMicroseconds;
    lap.start=ghost.start;lap.finish=ghost.finish;lap.samples=ghost.samples;lap.bridges=ghost.bridges;
    return target;
}
} // namespace scr::runtime

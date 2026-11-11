#pragma once
#include "state/native_checkpoint_io.h"
#include <functional>
#include <memory>

namespace scr::runtime {
using HallBytes=state::checkpoint::Bytes;
enum class HallArchiveState {Missing,Valid,Recovered,Corrupt,Unsupported};
template<class Model>struct HallArchiveReadFor {
    HallArchiveState state{HallArchiveState::Missing};
    std::shared_ptr<const Model> hall;std::uint64_t generation{};
};
enum class HallGhostState {Missing,Valid,Corrupt,Unsupported,Unavailable};
template<class Ghost>struct HallGhostReadFor {HallGhostState state{HallGhostState::Missing};std::shared_ptr<const Ghost> ghost;};
enum class HallWriteStage {GhostFlushed,GhostPublished,BackupFlushed,BackupPublished,HeadFlushed,HeadPublished,Collected};
using HallWriteObserver=std::function<void(HallWriteStage)>;
enum class HallSubmitResult {Queued,Duplicate,OutsideTable,Full};
} // namespace scr::runtime

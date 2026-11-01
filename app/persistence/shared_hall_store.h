#pragma once
#include "persistence/hall_ghost_codec.h"
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace scr::runtime {
using HallBytes=state::checkpoint::Bytes;
struct HallSubmission {HallAttempt attempt;HallBytes ghost;};
// Encodes and binds an optional recording; a time-only attempt has no reference.
HallSubmission makeHallSubmission(HallAttempt,const HallGhost* ghost=nullptr);
std::string hallObjectName(const HallAttemptId&);
enum class HallArchiveState {Missing,Valid,Recovered,Corrupt,Unsupported};
struct HallArchiveRead {
    HallArchiveState state{HallArchiveState::Missing};
    std::shared_ptr<const HallOfFame> hall;
    std::uint64_t generation{};
};
enum class HallGhostState {Missing,Valid,Corrupt,Unsupported,Unavailable};
struct HallGhostRead {HallGhostState state{HallGhostState::Missing};std::shared_ptr<const HallGhost> ghost;};
// Optional transaction observer for diagnostics and interrupted-write proofs.
enum class HallWriteStage {GhostFlushed,GhostPublished,BackupFlushed,BackupPublished,HeadFlushed,HeadPublished,Collected};
using HallWriteObserver=std::function<void(HallWriteStage)>;
class SharedHallArchive {
public:
    SharedHallArchive(std::filesystem::path root,HallCatalog,HallWriteObserver observer={});
    const std::filesystem::path& root()const noexcept{return root_;}
    HallArchiveRead read()const;
    HallGhostRead readGhost(const HallAttempt&)const;
    // Short locked transaction: merge latest, publish objects, backups, head, GC.
    // Exceptions leave the caller responsible for retrying these same attempts.
    std::uint64_t commit(const std::vector<std::shared_ptr<const HallSubmission>>&);
private:
    std::filesystem::path root_;
    HallCatalog catalog_;
    HallWriteObserver observer_;
};
enum class HallSubmitResult {Queued,Duplicate,OutsideTable,Full};
constexpr std::size_t hallPendingMaximumBytes=64*1024*1024;
// Off-thread publication. At most 20 pending attempts per board, with explicit
// backpressure. Full means the caller must retain the immutable attempt.
class SharedHallStore {
public:
    SharedHallStore(std::filesystem::path,HallCatalog,HallWriteObserver observer={},
        std::size_t pendingByteLimit=hallPendingMaximumBytes);
    ~SharedHallStore();
    SharedHallStore(const SharedHallStore&)=delete;
    SharedHallStore& operator=(const SharedHallStore&)=delete;
    HallSubmitResult submit(HallSubmission);
    void retry();
    bool flush();
    bool pending()const;
    bool failed()const;
    std::string error()const;
    HallArchiveRead read()const{return archive_.read();}
    HallGhostRead readGhost(const HallAttempt& a)const{return archive_.readGhost(a);}
private:
    SharedHallArchive archive_;
    HallCatalog catalog_;
    std::size_t limit_;
    mutable std::mutex mutex_;
    std::condition_variable changed_,idle_;
    std::vector<std::shared_ptr<const HallSubmission>> pending_,active_;
    std::string error_;
    bool stopping_{},writing_{},blocked_{};
    std::thread worker_;
    void run();
};
} // namespace scr::runtime

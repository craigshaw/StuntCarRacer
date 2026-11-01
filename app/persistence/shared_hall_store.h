#pragma once
#include "persistence/hall_ghost_codec.h"
#include "race/practice_ghost_target.h"
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace scr::runtime {
using HallBytes=state::checkpoint::Bytes;
struct HallSubmission {
    HallAttempt attempt;
    HallBytes ghost;
    // Live captures are encoded/hashed on the existing writer, not the drive loop.
    std::shared_ptr<const HallGhost> recording;
};
// Encodes and binds an optional recording; a time-only attempt has no reference.
HallSubmission makeHallSubmission(HallAttempt,const HallGhost* ghost=nullptr);
HallSubmission captureHallSubmission(HallAttempt,std::shared_ptr<const HallGhost>);
std::string hallObjectName(const HallAttemptId&);
enum class HallArchiveState {Missing,Valid,Recovered,Corrupt,Unsupported};
struct HallArchiveRead {
    HallArchiveState state{HallArchiveState::Missing};
    std::shared_ptr<const HallOfFame> hall;
    std::uint64_t generation{};
};
enum class HallGhostState {Missing,Valid,Corrupt,Unsupported,Unavailable};
struct HallGhostRead {HallGhostState state{HallGhostState::Missing};std::shared_ptr<const HallGhost> ghost;};
struct HallBrowserRead {
    HallBoardKey board;
    HallArchiveRead archive;
    std::array<bool,hallEntriesPerBoard> ghosts{};
    std::uint64_t request{};
    std::string error;
    std::optional<HallAttemptId> challenge;
    std::shared_ptr<const PinnedHallGhost> target;
};
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
constexpr std::size_t hallCaptureMaximumBytes=sizeof(HallGhost)+maxGhostLapSamples*sizeof(GhostLapSample)+
    maxGhostLapBridgeShapes*(sizeof(GhostBridgeShape)+512*sizeof(std::uint16_t));
// A lazy ceiling for all 320 possible pending lap recordings plus time-only rows.
// The live producer uses this bound so every valid top-20 candidate fits; no
// memory is reserved up front. Smaller budgets retain the explicit Full API.
constexpr std::size_t hallCapturePendingMaximumBytes=hallBoardCount*hallEntriesPerBoard*128+
    (hallBoardCount/2)*hallEntriesPerBoard*hallCaptureMaximumBytes;
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
    // A coalesced menu read on this same worker; no UI-thread file I/O.
    std::uint64_t requestBrowser(HallBoardKey,std::optional<HallAttemptId> challenge={});
    std::shared_ptr<const HallBrowserRead> browserRead()const;
    void cancelBrowser();
private:
    SharedHallArchive archive_;
    HallCatalog catalog_;
    std::size_t limit_;
    mutable std::mutex mutex_;
    std::condition_variable changed_,idle_;
    std::vector<std::shared_ptr<const HallSubmission>> pending_,active_;
    std::string error_;
    bool stopping_{},writing_{},blocked_{},checkRequested_{};
    std::optional<HallBoardKey> browserRequest_;
    std::optional<HallAttemptId> browserChallenge_;
    std::uint64_t browserSerial_{};
    std::shared_ptr<const HallBrowserRead> browserRead_;
    std::thread worker_;
    void run();
};
} // namespace scr::runtime

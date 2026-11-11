#pragma once
#include "persistence/enhanced_record_codec.h"
#include "persistence/hall_storage_types.h"
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace scr::runtime {
using EnhancedHallRead=HallArchiveReadFor<EnhancedHallOfFame>;
using EnhancedGhostRead=HallGhostReadFor<EnhancedHallGhost>;
struct EnhancedSubmission {
    EnhancedHallAttempt attempt;HallBytes ghost;
    std::shared_ptr<const EnhancedHallGhost> recording;
};
EnhancedSubmission makeEnhancedSubmission(EnhancedHallAttempt,const EnhancedHallGhost* ghost=nullptr);
EnhancedSubmission captureEnhancedSubmission(EnhancedHallAttempt,std::shared_ptr<const EnhancedHallGhost>);
// Always a sibling namespace, for both installed and --data-dir Original roots.
std::filesystem::path enhancedHallRoot(const std::filesystem::path& originalRoot);
std::string enhancedObjectName(const HallAttemptId&);
HallAttemptId enhancedCompletionId(const HallAttemptId& run,std::uint64_t crossing,HallRecordKind,unsigned method=enhancedTimingMethodVersion);
class EnhancedHallArchive {
public:
    EnhancedHallArchive(std::filesystem::path originalRoot,EnhancedCatalog,HallWriteObserver observer={});
    const std::filesystem::path& root()const noexcept{return root_;}
    EnhancedHallRead read()const;
    EnhancedGhostRead readGhost(const EnhancedHallAttempt&)const;
    std::uint64_t commit(const std::vector<std::shared_ptr<const EnhancedSubmission>>&);
private:
    std::filesystem::path root_;EnhancedCatalog catalog_;HallWriteObserver observer_;
};
enum class EnhancedSubmitResult {Queued,Duplicate,OutsideTable,Full,Ineligible};
constexpr std::size_t enhancedPendingMaximumBytes=64*1024*1024;
constexpr std::size_t enhancedCaptureMaximumBytes=sizeof(EnhancedHallGhost)+maxGhostLapSamples*(sizeof(GhostLapSample)+sizeof(std::int64_t))+
    maxGhostLapBridgeShapes*(sizeof(GhostBridgeShape)+512*sizeof(std::uint16_t));
// Permit normal geometric vector growth, and charge the actual allocation.
constexpr std::size_t enhancedEncodedAllocationMaximumBytes=2*enhancedHallGhostMaximumBytes;
constexpr std::size_t enhancedPendingRecordMaximumBytes=enhancedCaptureMaximumBytes>enhancedEncodedAllocationMaximumBytes?enhancedCaptureMaximumBytes:enhancedEncodedAllocationMaximumBytes;
constexpr std::size_t enhancedCapturePendingMaximumBytes=hallBoardCount*hallEntriesPerBoard*256+(hallBoardCount/2)*hallEntriesPerBoard*enhancedPendingRecordMaximumBytes;
class EnhancedHallStore {
public:
    EnhancedHallStore(std::filesystem::path originalRoot,EnhancedCatalog,HallWriteObserver observer={},std::size_t byteLimit=enhancedPendingMaximumBytes);
    ~EnhancedHallStore();
    EnhancedHallStore(const EnhancedHallStore&)=delete;
    EnhancedHallStore& operator=(const EnhancedHallStore&)=delete;
    EnhancedSubmitResult submit(EnhancedSubmission);
    void retry();bool flush();bool pending()const;bool failed()const;std::string error()const;
    EnhancedHallRead read()const{return archive_.read();}
    EnhancedGhostRead readGhost(const EnhancedHallAttempt& a)const{return archive_.readGhost(a);}
private:
    EnhancedHallArchive archive_;EnhancedCatalog catalog_;std::size_t limit_;
    mutable std::mutex mutex_;std::condition_variable changed_,idle_;std::thread worker_;
    std::vector<std::shared_ptr<const EnhancedSubmission>> pending_,active_;
    bool stopping_{},writing_{},blocked_{};std::string error_;
    void run();
};
} // namespace scr::runtime

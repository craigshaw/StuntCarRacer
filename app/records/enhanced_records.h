#pragma once
#include "records/enhanced_time.h"
#include "records/hall_of_fame.h"
#include "race/ghost_lap_data.h"
#include <map>
#include <memory>
#include <stdexcept>

namespace scr::runtime {
struct EnhancedCatalog {
    std::uint64_t trackContent{};
    std::uint32_t rules{},timingMethod{enhancedTimingMethodVersion};
};
class UnsupportedEnhancedRecords:public std::runtime_error {public:using std::runtime_error::runtime_error;};
void validateEnhancedCatalog(EnhancedCatalog);
bool sameEnhancedCatalog(EnhancedCatalog,EnhancedCatalog)noexcept;
struct EnhancedRecording {
    EnhancedTime time;
    LapTimingBoundary start,finish;
    std::vector<GhostLapSample> samples;
    std::vector<GhostBridgeShape> bridges;
    // Elapsed movement offsets, in native event order; penalties never move poses.
    std::vector<std::int64_t> recoveryOffsets;
};
void validateEnhancedRecording(const EnhancedRecording&,unsigned track);
struct EnhancedBestKey {
    std::string career;unsigned track{};bool superLeague{};
    bool operator<(const EnhancedBestKey&)const noexcept;
};
void validateEnhancedBestKey(const EnhancedBestKey&);
struct EnhancedBest {
    EnhancedCatalog catalog;
    EnhancedBestKey key;
    EnhancedTime time;
    std::shared_ptr<const EnhancedRecording> recording;
};
void validateEnhancedBest(const EnhancedBest&);
class EnhancedPersonalBests {
public:
    explicit EnhancedPersonalBests(EnhancedCatalog);
    EnhancedCatalog catalog()const noexcept{return catalog_;}
    std::shared_ptr<const EnhancedBest> best(const EnhancedBestKey&)const;
    bool offer(EnhancedBest);
    void forget(const EnhancedBestKey& key){bests_.erase(key);}
    void forgetCareer(const std::string&);
    std::size_t size()const noexcept{return bests_.size();}
private:
    EnhancedCatalog catalog_;
    std::map<EnhancedBestKey,std::shared_ptr<const EnhancedBest>> bests_;
};
struct EnhancedHallAttempt {
    HallAttemptId id{};HallBoardKey board;std::string name;
    EnhancedTime time;
    std::optional<HallGhostDigest> ghost;
};
struct EnhancedHallEntry {EnhancedHallAttempt attempt;std::uint64_t insertionOrder{};};
// Presentation-only record achievements from one race, independent of native
// record flags and retained with the career's post-race continuation.
struct EnhancedRecordLine {std::string name;std::int64_t milliseconds{};};
struct EnhancedRecordNotice {
    std::optional<EnhancedRecordLine> lap,race;
    bool any()const noexcept{return lap.has_value()||race.has_value();}
};
struct EnhancedHallSnapshot {
    EnhancedCatalog catalog;std::uint64_t nextInsertionOrder{1};
    std::array<std::vector<EnhancedHallEntry>,hallBoardCount> boards;
};
enum class EnhancedAdmission {Accepted,Duplicate,OutsideTable,Ineligible};
class EnhancedHallOfFame {
public:
    explicit EnhancedHallOfFame(EnhancedCatalog);
    explicit EnhancedHallOfFame(EnhancedHallSnapshot);
    EnhancedAdmission offer(EnhancedHallAttempt);
    const std::vector<EnhancedHallEntry>& entries(HallBoardKey)const;
    unsigned rank(HallBoardKey,unsigned row)const;
    const EnhancedHallSnapshot& snapshot()const noexcept{return state_;}
private:
    EnhancedHallSnapshot state_;
};
std::string formatEnhancedTime(const EnhancedTime&);
std::string formatEnhancedMilliseconds(std::int64_t);
} // namespace scr::runtime

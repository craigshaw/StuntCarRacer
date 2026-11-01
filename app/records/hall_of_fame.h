#pragma once
#include "race/race_timing.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scr::runtime {
enum class HallLeague : std::uint8_t { Normal,Super };
enum class HallRecordKind : std::uint8_t { Lap,Race };
struct HallBoardKey {
    unsigned track{};
    HallLeague league{};
    HallRecordKind kind{};
};
constexpr unsigned hallBoardCount=32,hallEntriesPerBoard=20;
using HallAttemptId=std::array<std::uint8_t,16>;
using HallGhostDigest=std::array<std::uint8_t,32>;
struct HallName {std::string display,identity;};
HallName normaliseHallName(std::string_view);
unsigned hallBoardIndex(HallBoardKey);
HallBoardKey hallBoardKey(unsigned index);
unsigned hallTimeHundredths(PackedLapTime,HallRecordKind);
std::string formatHallTime(PackedLapTime,HallRecordKind);
struct HallCatalog {
    std::uint64_t trackContent{};
    std::uint32_t rules{};
};
struct HallAttempt {
    HallAttemptId id{};
    HallBoardKey board;
    std::string name;
    PackedLapTime time{};
    // A reference to a captured recording, not a claim that its file is usable.
    // The future reader resolves availability separately. Filename is the ID.
    std::optional<HallGhostDigest> ghost;
};
struct HallEntry {HallAttempt attempt;std::uint64_t insertionOrder{};};
struct HallSnapshot {
    HallCatalog catalog;
    std::uint64_t nextInsertionOrder{1};
    std::array<std::vector<HallEntry>,hallBoardCount> boards;
};
enum class HallAdmission { Accepted,Duplicate,OutsideTable };
// Pure bounded ranking state. No clock, random number source, filesystem,
// career ownership or simulation writes. Callers supply retry-stable IDs.
class HallOfFame {
public:
    explicit HallOfFame(HallCatalog);
    explicit HallOfFame(HallSnapshot);
    HallAdmission offer(HallAttempt);
    const std::vector<HallEntry>& entries(HallBoardKey)const;
    unsigned rank(HallBoardKey,unsigned row)const;
    const HallSnapshot& snapshot()const noexcept{return state_;}
private:
    HallSnapshot state_;
};
} // namespace scr::runtime

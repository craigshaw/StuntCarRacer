#include "persistence/hall_table_codec.h"

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x4c414853; // SHAL; not the old native Hall envelope.
void check(bool value){if(!value)throw std::runtime_error("Invalid shared Hall table");}
}
state::checkpoint::Bytes encodeHallTable(const HallOfFame& hall){
    const auto& state=hall.snapshot();Writer w;
    w.u32(magic);w.u32(hallTableFormatVersion);w.u32(state.catalog.rules);w.u64(state.catalog.trackContent);w.u64(state.nextInsertionOrder);
    for(const auto& board:state.boards){w.u32(unsigned(board.size()));for(const auto& entry:board){
        const auto& a=entry.attempt;w.bytes(a.id.data(),a.id.size());w.text(a.name);
        w.u32((unsigned(a.time.minutes)<<16)|(unsigned(a.time.seconds)<<8)|a.time.subseconds);w.u64(entry.insertionOrder);
        w.u32(a.ghost?1:0);if(a.ghost)w.bytes(a.ghost->data(),a.ghost->size());
    }}
    w.u64(hash(w.data.data(),w.data.size()));check(w.data.size()<=hallTableMaximumBytes);return std::move(w.data);
}
HallOfFame decodeHallTable(const state::checkpoint::Bytes& bytes,HallCatalog expected){
    check(bytes.size()>=36&&bytes.size()<=hallTableMaximumBytes);Reader r{bytes};check(r.u32()==magic);
    if(r.u32()!=hallTableFormatVersion)throw UnsupportedHallTable("Unsupported shared Hall format");
    HallSnapshot state;state.catalog.rules=r.u32();state.catalog.trackContent=r.u64();state.nextInsertionOrder=r.u64();
    if(state.catalog.rules!=expected.rules||state.catalog.trackContent!=expected.trackContent)throw UnsupportedHallTable("Shared Hall belongs to different track content or rules");
    Reader tail{bytes,bytes.size()-8};check(tail.u64()==hash(bytes.data(),bytes.size()-8));
    for(unsigned n=0;n<hallBoardCount;++n){const auto count=r.u32();check(count<=hallEntriesPerBoard);
        for(unsigned i=0;i<count;++i){HallEntry e;e.attempt.board=hallBoardKey(n);r.bytes(e.attempt.id.data(),e.attempt.id.size());e.attempt.name=r.text(12);
            const auto time=r.u32();check(time<=0xffffff);e.attempt.time={std::uint8_t(time>>16),std::uint8_t(time>>8),std::uint8_t(time)};
            e.insertionOrder=r.u64();const auto ghost=r.u32();check(ghost<=1);if(ghost){e.attempt.ghost.emplace();r.bytes(e.attempt.ghost->data(),e.attempt.ghost->size());}
            state.boards[n].push_back(std::move(e));
        }
    }
    check(r.position==bytes.size()-8);(void)r.u64();r.end();return HallOfFame(std::move(state));
}
} // namespace scr::runtime

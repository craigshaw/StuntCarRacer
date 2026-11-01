#include "records/hall_of_fame.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned decimal(std::uint8_t value){
    if((value&15)>9||(value>>4)>9)throw std::invalid_argument("Invalid Hall BCD time");
    return (value>>4)*10+(value&15);
}
template<class T>bool nonzero(const T& bytes){return std::any_of(bytes.begin(),bytes.end(),[](auto b){return b!=0;});}
void catalog(HallCatalog value){if(!value.trackContent||!value.rules)throw std::invalid_argument("Missing Hall compatibility identity");}
void validate(const HallAttempt& attempt){
    (void)hallBoardIndex(attempt.board);(void)hallTimeHundredths(attempt.time,attempt.board.kind);
    if(!nonzero(attempt.id))throw std::invalid_argument("Empty Hall attempt ID");
    if(normaliseHallName(attempt.name).display!=attempt.name)throw std::invalid_argument("Hall display name is not normalised");
    if(attempt.ghost&&(attempt.board.kind!=HallRecordKind::Lap||!nonzero(*attempt.ghost)))throw std::invalid_argument("Invalid Hall ghost reference");
}
bool same(const HallAttempt& a,const HallAttempt& b){
    return a.id==b.id&&hallBoardIndex(a.board)==hallBoardIndex(b.board)&&a.name==b.name&&
        a.time.minutes==b.time.minutes&&a.time.seconds==b.time.seconds&&a.time.subseconds==b.time.subseconds&&a.ghost==b.ghost;
}
}
HallName normaliseHallName(std::string_view input){
    if(input.empty()||input.size()>12)throw std::invalid_argument("Hall name exceeds native driver field");
    HallName result;bool space=false;
    for(unsigned char c:input){
        if(c==' '){space=!result.display.empty();continue;}
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='/'||c==':'))
            throw std::invalid_argument("Hall name contains an unsupported character");
        if(space){result.display+=' ';result.identity+=' ';space=false;}
        result.display+=char(c);result.identity+=char(c>='A'&&c<='Z'?c-'A'+'a':c);
    }
    if(result.display.empty())throw std::invalid_argument("Empty Hall name");
    return result;
}
unsigned hallBoardIndex(HallBoardKey key){
    if(key.track>=8||unsigned(key.league)>1||unsigned(key.kind)>1)throw std::invalid_argument("Invalid Hall board");
    return key.track*4+unsigned(key.league)*2+unsigned(key.kind);
}
HallBoardKey hallBoardKey(unsigned index){
    if(index>=hallBoardCount)throw std::invalid_argument("Invalid Hall board index");
    return {index/4,HallLeague((index/2)%2),HallRecordKind(index%2)};
}
unsigned hallTimeHundredths(PackedLapTime time,HallRecordKind kind){
    if(unsigned(kind)>1)throw std::invalid_argument("Invalid Hall record kind");
    const auto minutes=decimal(time.minutes),seconds=decimal(time.seconds),sub=decimal(time.subseconds);
    const auto total=(minutes*60+seconds)*100+sub;
    // Native lap minutes hold at nine. The race result adds the first three
    // accepted packed-decimal laps; it is not limited to one minute digit.
    if(seconds>=60||total>(kind==HallRecordKind::Lap?59999u:179997u))throw std::invalid_argument("Hall time outside native clock bounds");
    return total;
}
std::string formatHallTime(PackedLapTime time,HallRecordKind kind){
    const auto total=hallTimeHundredths(time,kind);std::ostringstream out;
    out<<total/6000<<':'<<std::setw(2)<<std::setfill('0')<<(total/100)%60<<'.'<<std::setw(2)<<total%100;return out.str();
}
HallOfFame::HallOfFame(HallCatalog value){catalog(value);state_.catalog=value;}
HallOfFame::HallOfFame(HallSnapshot value):state_(std::move(value)){
    catalog(state_.catalog);
    if(!state_.nextInsertionOrder)throw std::invalid_argument("Invalid Hall insertion sequence");
    std::set<HallAttemptId> ids;std::set<std::uint64_t> orders;
    for(unsigned n=0;n<hallBoardCount;++n){const auto& board=state_.boards[n];
        if(board.size()>hallEntriesPerBoard)throw std::invalid_argument("Hall table exceeds its entry limit");
        unsigned previous=0;std::uint64_t previousOrder=0;bool first=true;
        for(const auto& entry:board){validate(entry.attempt);const auto time=hallTimeHundredths(entry.attempt.time,entry.attempt.board.kind);
            if(hallBoardIndex(entry.attempt.board)!=n||!entry.insertionOrder||entry.insertionOrder>=state_.nextInsertionOrder||
               !ids.insert(entry.attempt.id).second||!orders.insert(entry.insertionOrder).second||
               (!first&&(time<previous||(time==previous&&entry.insertionOrder<=previousOrder))))
                throw std::invalid_argument("Inconsistent Hall snapshot");
            first=false;previous=time;previousOrder=entry.insertionOrder;
        }
    }
}
HallAdmission HallOfFame::offer(HallAttempt attempt){
    attempt.name=normaliseHallName(attempt.name).display;validate(attempt);
    for(const auto& board:state_.boards)for(const auto& entry:board)if(entry.attempt.id==attempt.id){
        if(!same(entry.attempt,attempt))throw std::invalid_argument("Hall attempt ID was reused with different content");
        return HallAdmission::Duplicate;
    }
    auto& board=state_.boards[hallBoardIndex(attempt.board)];const auto time=hallTimeHundredths(attempt.time,attempt.board.kind);
    if(board.size()==hallEntriesPerBoard&&time>=hallTimeHundredths(board.back().attempt.time,attempt.board.kind))return HallAdmission::OutsideTable;
    if(state_.nextInsertionOrder==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Hall insertion sequence exhausted");
    const auto at=std::upper_bound(board.begin(),board.end(),time,[](unsigned t,const HallEntry& e){return t<hallTimeHundredths(e.attempt.time,e.attempt.board.kind);});
    board.insert(at,{std::move(attempt),state_.nextInsertionOrder});++state_.nextInsertionOrder;
    if(board.size()>hallEntriesPerBoard)board.pop_back();
    return HallAdmission::Accepted;
}
const std::vector<HallEntry>& HallOfFame::entries(HallBoardKey key)const{return state_.boards.at(hallBoardIndex(key));}
unsigned HallOfFame::rank(HallBoardKey key,unsigned row)const{
    const auto& board=entries(key);const auto time=hallTimeHundredths(board.at(row).attempt.time,key.kind);unsigned result=1;
    for(const auto& entry:board){if(hallTimeHundredths(entry.attempt.time,key.kind)>=time)break;++result;}
    return result;
}
} // namespace scr::runtime

#include "records/enhanced_records.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace scr::runtime {
namespace {
void require(bool value){if(!value)throw std::invalid_argument("Invalid Enhanced record");}
template<class T>bool nonzero(const T& bytes){return std::any_of(bytes.begin(),bytes.end(),[](auto b){return b!=0;});}
void validate(const EnhancedHallAttempt& a){
    (void)hallBoardIndex(a.board);validateEnhancedTime(a.time);require(nonzero(a.id));
    require(normaliseHallName(a.name).display==a.name);
    require(!a.ghost||(a.board.kind==HallRecordKind::Lap&&nonzero(*a.ghost)));
}
bool same(const EnhancedHallAttempt& a,const EnhancedHallAttempt& b){
    return a.id==b.id&&hallBoardIndex(a.board)==hallBoardIndex(b.board)&&a.name==b.name&&sameEnhancedTime(a.time,b.time)&&a.ghost==b.ghost;
}
}
void validateEnhancedCatalog(EnhancedCatalog catalog){
    require(catalog.trackContent&&catalog.rules);
    if(catalog.timingMethod!=enhancedTimingMethodVersion)throw UnsupportedEnhancedRecords("Unsupported Enhanced timing method");
}
bool sameEnhancedCatalog(EnhancedCatalog a,EnhancedCatalog b)noexcept{return a.trackContent==b.trackContent&&a.rules==b.rules&&a.timingMethod==b.timingMethod;}
bool EnhancedBestKey::operator<(const EnhancedBestKey& b)const noexcept{return std::tie(career,track,superLeague)<std::tie(b.career,b.track,b.superLeague);}
void validateEnhancedBestKey(const EnhancedBestKey& key){
    require(key.track<8&&key.career.size()==32&&std::all_of(key.career.begin(),key.career.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}));
}
void validateEnhancedRecording(const EnhancedRecording& g,unsigned track){
    require(track<8);validateEnhancedTime(g.time);
    for(const auto* b:{&g.start,&g.finish})require(b->confidence==timing::Reason::reliable&&b->crossingMicroseconds>=0&&
        b->observedMicroseconds>=b->crossingMicroseconds&&b->observedMicroseconds-b->crossingMicroseconds<=timing::tickUs);
    require(sameEnhancedTime(measureEnhancedTime(g.start,g.finish),g.time));
    const auto duration=g.time.elapsedMicroseconds;
    require(duration<=std::int64_t(maxGhostLapSamples)*timing::tickUs);
    require(g.samples.size()>=2&&g.samples.size()<=maxGhostLapSamples&&g.bridges.size()<=maxGhostLapBridgeShapes&&g.recoveryOffsets.size()<=maxGhostLapSamples);
    require(g.time.recoveries==g.recoveryOffsets.size());
    require(g.samples.front().microseconds>=-timing::tickUs&&g.samples.front().microseconds<=0&&g.samples.back().microseconds>=duration&&g.samples.back().microseconds<=duration+timing::tickUs);
    auto previous=g.samples.front().microseconds;
    std::set<std::int64_t> cuts;
    for(const auto& s:g.samples){
        require(s.microseconds>=previous&&s.microseconds<=duration+timing::tickUs&&s.microseconds-previous<=timing::tickUs&&s.segment<100&&unsigned(s.region)<=3);
        require(s.bridge==noGhostBridge||s.bridge<g.bridges.size());
        require(track==5?s.bridge!=noGhostBridge:s.bridge==noGhostBridge&&s.region==GhostBridgeRegion::outside);
        if(s.cut)cuts.insert(s.microseconds);previous=s.microseconds;
    }
    require(track==5?!g.bridges.empty():g.bridges.empty());
    for(const auto& b:g.bridges){require(!b.cachedSections.empty()&&b.cachedSections.size()<=510&&b.cachedSections.size()%3==0);
        for(unsigned n=0;n<b.cachedSections.size();n+=3)require((b.cachedSections[n]&255)>=0x33&&(b.cachedSections[n]&255)<0x38);}
    previous=0;for(auto offset:g.recoveryOffsets){require(offset>=previous&&offset<=duration&&cuts.count(offset));previous=offset;}
}
void validateEnhancedBest(const EnhancedBest& best){
    validateEnhancedCatalog(best.catalog);validateEnhancedBestKey(best.key);validateEnhancedTime(best.time);
    require(best.time.methodVersion==best.catalog.timingMethod);
    if(best.recording){validateEnhancedRecording(*best.recording,best.key.track);require(sameEnhancedTime(best.time,best.recording->time));}
}
EnhancedPersonalBests::EnhancedPersonalBests(EnhancedCatalog catalog):catalog_(catalog){validateEnhancedCatalog(catalog_);}
std::shared_ptr<const EnhancedBest> EnhancedPersonalBests::best(const EnhancedBestKey& key)const{
    const auto found=bests_.find(key);return found==bests_.end()?nullptr:found->second;
}
bool EnhancedPersonalBests::offer(EnhancedBest value){
    validateEnhancedCatalog(value.catalog);validateEnhancedBestKey(value.key);
    if(!sameEnhancedCatalog(value.catalog,catalog_))throw UnsupportedEnhancedRecords("Enhanced PB compatibility differs");
    if(!value.time.rankable())return false;
    validateEnhancedBest(value);const auto old=best(value.key);
    if(old&&old->time.milliseconds()<=value.time.milliseconds())return false;
    const auto key=value.key;bests_[key]=std::make_shared<const EnhancedBest>(std::move(value));return true;
}
void EnhancedPersonalBests::forgetCareer(const std::string& career){for(auto it=bests_.begin();it!=bests_.end();)if(it->first.career==career)it=bests_.erase(it);else ++it;}
EnhancedHallOfFame::EnhancedHallOfFame(EnhancedCatalog catalog){validateEnhancedCatalog(catalog);state_.catalog=catalog;}
EnhancedHallOfFame::EnhancedHallOfFame(EnhancedHallSnapshot value):state_(std::move(value)){
    validateEnhancedCatalog(state_.catalog);require(state_.nextInsertionOrder!=0);std::set<HallAttemptId> ids;std::set<std::uint64_t> orders;
    for(unsigned n=0;n<hallBoardCount;++n){const auto& board=state_.boards[n];require(board.size()<=hallEntriesPerBoard);
        std::int64_t previous=0;std::uint64_t order=0;bool first=true;
        for(const auto& entry:board){validate(entry.attempt);const auto time=entry.attempt.time.milliseconds();
            require(hallBoardIndex(entry.attempt.board)==n&&entry.attempt.time.methodVersion==state_.catalog.timingMethod&&entry.insertionOrder&&entry.insertionOrder<state_.nextInsertionOrder&&
                ids.insert(entry.attempt.id).second&&orders.insert(entry.insertionOrder).second&&(first||time>previous||(time==previous&&entry.insertionOrder>order)));
            first=false;previous=time;order=entry.insertionOrder;}}
}
EnhancedAdmission EnhancedHallOfFame::offer(EnhancedHallAttempt attempt){
    attempt.name=normaliseHallName(attempt.name).display;(void)hallBoardIndex(attempt.board);require(nonzero(attempt.id));
    if(!attempt.time.rankable())return EnhancedAdmission::Ineligible;
    validate(attempt);require(attempt.time.methodVersion==state_.catalog.timingMethod);
    for(const auto& board:state_.boards)for(const auto& entry:board)if(entry.attempt.id==attempt.id){require(same(entry.attempt,attempt));return EnhancedAdmission::Duplicate;}
    auto& board=state_.boards[hallBoardIndex(attempt.board)];const auto time=attempt.time.milliseconds();
    if(board.size()==hallEntriesPerBoard&&time>=board.back().attempt.time.milliseconds())return EnhancedAdmission::OutsideTable;
    if(state_.nextInsertionOrder==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Enhanced Hall insertion sequence exhausted");
    const auto at=std::upper_bound(board.begin(),board.end(),time,[](auto t,const auto& entry){return t<entry.attempt.time.milliseconds();});
    board.insert(at,{std::move(attempt),state_.nextInsertionOrder++});if(board.size()>hallEntriesPerBoard)board.pop_back();return EnhancedAdmission::Accepted;
}
const std::vector<EnhancedHallEntry>& EnhancedHallOfFame::entries(HallBoardKey board)const{return state_.boards.at(hallBoardIndex(board));}
unsigned EnhancedHallOfFame::rank(HallBoardKey key,unsigned row)const{
    const auto& board=entries(key);const auto time=board.at(row).attempt.time.milliseconds();unsigned result=1;
    for(const auto& entry:board){if(entry.attempt.time.milliseconds()>=time)break;++result;}return result;
}
std::string formatEnhancedMilliseconds(std::int64_t milliseconds){require(milliseconds>=0);std::ostringstream out;
    out<<milliseconds/60000<<':'<<std::setw(2)<<std::setfill('0')<<(milliseconds/1000)%60<<'.'<<std::setw(3)<<milliseconds%1000;return out.str();}
std::string formatEnhancedTime(const EnhancedTime& time){validateEnhancedTime(time);return formatEnhancedMilliseconds(time.milliseconds());}
} // namespace scr::runtime

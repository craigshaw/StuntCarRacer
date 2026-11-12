#include "menus/hall_browser.h"

namespace scr::runtime {
namespace {
// Division 4 through Division 1. Archive keys retain their native track IDs.
constexpr std::array<unsigned,8> trackOrder{{0,2,1,3,6,7,4,5}};
constexpr std::array<HallBrowserFocus,6> focusOrder{{HallBrowserFocus::Track,HallBrowserFocus::League,HallBrowserFocus::Timing,HallBrowserFocus::Category,HallBrowserFocus::Records,HallBrowserFocus::Page}};
}
void HallBrowser::open(std::string driver,HallBoardKey initial,const NativeMenuInput& held,TimingCategory timing){
    (void)hallBoardIndex(initial);driver_=std::move(driver);
    if(!initialised_){board_=initial;initialised_=true;}
    if(timing_!=timing){timing_=timing;resetRows();}
    loading_=true;clock_.enterScreen(held);
}
const std::vector<EnhancedHallEntry>& HallBrowser::enhancedEntries()const noexcept{static const std::vector<EnhancedHallEntry> empty;return enhancedHall_?enhancedHall_->entries(board_):empty;}
unsigned HallBrowser::rowCount()const noexcept{return unsigned(timing_==TimingCategory::Original?entries().size():enhancedEntries().size());}
std::string HallBrowser::rowName(unsigned row)const{return timing_==TimingCategory::Original?entries().at(row).attempt.name:enhancedEntries().at(row).attempt.name;}
std::string HallBrowser::rowTime(unsigned row)const{return timing_==TimingCategory::Original?formatHallTime(entries().at(row).attempt.time,board_.kind):formatEnhancedTime(enhancedEntries().at(row).attempt.time);}
HallAttemptId HallBrowser::rowId(unsigned row)const{return timing_==TimingCategory::Original?entries().at(row).attempt.id:enhancedEntries().at(row).attempt.id;}
const std::vector<HallEntry>& HallBrowser::entries()const noexcept{
    static const std::vector<HallEntry> empty;return hall_?hall_->entries(board_):empty;
}
void HallBrowser::selection(unsigned row){
    selected_=rowCount()?std::min(row,rowCount()-1):0;
    selectedId_=rowCount()?std::optional<HallAttemptId>{rowId(selected_)}:std::nullopt;
}
void HallBrowser::apply(const HallBrowserRead& read){
    if(timing_!=TimingCategory::Original||hallBoardIndex(read.board)!=hallBoardIndex(board_))return;
    loading_=false;unavailable_=!read.error.empty()||!read.archive.hall;
    if(unavailable_){hall_.reset();ghosts_.fill(false);if(focus_==HallBrowserFocus::Records)focus_=HallBrowserFocus::Category;return;}
    hall_=read.archive.hall;ghosts_=read.ghosts;
    if(selectedId_){const auto& board=entries();const auto found=std::find_if(board.begin(),board.end(),[&](const auto& e){return e.attempt.id==*selectedId_;});
        if(found!=board.end()){selection(unsigned(found-board.begin()));return;}}
    selection(selected_);
    if(entries().empty()&&focus_==HallBrowserFocus::Records)focus_=HallBrowserFocus::Page;
}
void HallBrowser::apply(const EnhancedBrowserRead& read){
    if(timing_!=TimingCategory::Enhanced||hallBoardIndex(read.board)!=hallBoardIndex(board_))return;
    loading_=false;unavailable_=!read.error.empty()||!read.archive.hall;
    if(unavailable_){enhancedHall_.reset();ghosts_.fill(false);if(focus_==HallBrowserFocus::Records)focus_=HallBrowserFocus::Category;return;}
    enhancedHall_=read.archive.hall;ghosts_=read.ghosts;
    if(selectedId_)for(unsigned n=0;n<rowCount();++n)if(rowId(n)==*selectedId_){selection(n);return;}
    selection(selected_);if(!rowCount()&&focus_==HallBrowserFocus::Records)focus_=HallBrowserFocus::Page;
}
void HallBrowser::resetRows(){selected_=0;selectedId_.reset();hall_.reset();enhancedHall_.reset();ghosts_.fill(false);unavailable_=false;loading_=true;}
void HallBrowser::move(int direction){
    const auto first=page()*10,last=std::min(first+10,rowCount());
    if(focus_==HallBrowserFocus::Records){
        if(direction<0){if(selected_>first)selection(selected_-1);else focus_=HallBrowserFocus::Category;}
        else{if(selected_+1<last)selection(selected_+1);else focus_=HallBrowserFocus::Page;}return;
    }
    const auto current=std::find(focusOrder.begin(),focusOrder.end(),focus_)-focusOrder.begin();const auto next=int(current)+direction;if(next<0||next>=int(focusOrder.size()))return;
    focus_=focusOrder[unsigned(next)];
    if(focus_==HallBrowserFocus::Records){
        if(!rowCount())focus_=direction>0?HallBrowserFocus::Page:HallBrowserFocus::Category;
        else selection(direction>0?std::clamp(selected_,first,last-1):last-1);
    }
}
void HallBrowser::changePage(int direction){
    const auto next=std::clamp(int(page())+direction,0,int(pages())-1);
    if(unsigned(next)!=page())selection(unsigned(next)*10+selected_%10);
}
HallBrowserAction HallBrowser::update(double elapsed,const NativeMenuInput& held){
    const auto input=clock_.sample(held,elapsed);if(input.back)return HallBrowserAction::Back;
    if(input.numberedKeys&1)focus_=HallBrowserFocus::Track;
    if(input.numberedKeys&2)focus_=HallBrowserFocus::League;
    if(input.numberedKeys&4)focus_=HallBrowserFocus::Category;
    if(input.numberedKeys&8)focus_=HallBrowserFocus::Timing;
    if(input.controls&1)move(-1);else if(input.controls&2)move(1);
    int direction=(input.controls&4)?-1:(input.controls&8)?1:0;
    if(input.controls&16){
        if(canChallenge())return HallBrowserAction::Challenge;
        if(focus_!=HallBrowserFocus::Records)direction=1;
    }
    if(!direction)return HallBrowserAction::None;
    switch(focus_){
    case HallBrowserFocus::Track:{
        const auto ordinal=std::find(trackOrder.begin(),trackOrder.end(),board_.track)-trackOrder.begin();
        board_.track=trackOrder[unsigned((int(ordinal)+direction+8)%8)];break;
    }
    case HallBrowserFocus::League:board_.league=board_.league==HallLeague::Normal?HallLeague::Super:HallLeague::Normal;break;
    case HallBrowserFocus::Category:board_.kind=board_.kind==HallRecordKind::Lap?HallRecordKind::Race:HallRecordKind::Lap;break;
    case HallBrowserFocus::Timing:timing_=timing_==TimingCategory::Original?TimingCategory::Enhanced:TimingCategory::Original;break;
    case HallBrowserFocus::Records:case HallBrowserFocus::Page:changePage(direction);return HallBrowserAction::None;
    default:return HallBrowserAction::None;
    }
    resetRows();
    return HallBrowserAction::Load;
}
} // namespace scr::runtime

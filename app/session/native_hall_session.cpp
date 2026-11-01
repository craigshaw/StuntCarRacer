#include "session/native_game_session.h"

namespace scr::runtime {
void NativeGameSession::requestHallBrowser(){
    hallBrowser_.loading();hallBrowserRequest_=sharedHallStore_->requestBrowser(hallBrowser_.board());
}
void NativeGameSession::openHallBrowser(const NativeMenuInput& held){
    if(!activeCareer_||multiplayer()||!sharedHallStore_)throw std::logic_error("Hall browser requires an active career");
    HallBoardKey initial{unsigned((*data_.memory)[0x1ca33])%8,activeCareer_->league?HallLeague::Super:HallLeague::Normal,HallRecordKind::Lap};
    hallBrowser_.open(activeCareer_->name,initial,held);hallBrowserActive_=true;requestHallBrowser();
}
bool NativeGameSession::updateHallBrowser(double elapsed,const NativeGameInput& input,NativeGameUpdate& result){
    if(!hallBrowserActive_)return false;
    if(transitioning()){hallBrowser_.block(input.menu);return false;}
    const auto read=sharedHallStore_->browserRead();
    if(read&&read->request==hallBrowserRequest_&&read->request!=hallBrowserApplied_){hallBrowser_.apply(*read);hallBrowserApplied_=read->request;}
    const auto action=hallBrowser_.update(elapsed,input.menu);
    if(action==HallBrowserAction::Load)requestHallBrowser();
    else if(action==HallBrowserAction::Back){
        const auto previous=menuImage();hallBrowserActive_=false;sharedHallStore_->cancelBrowser();
        menu_->setNavigation(NativeMenuNavigation::Responsive);(void)menu_->back();menu_->setNavigation(navigation_);
        (*data_.memory)[0x5d724]=0x41;
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());fade_.fadeToColor(0);transition_=Transition::OutHallMenu;
        saveCheckpoint();releaseInput(input.menu);
        if(navigation_==NativeMenuNavigation::Responsive)finishResponsiveTransitions(result);
    }
    return true;
}
} // namespace scr::runtime

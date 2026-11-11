#include "session/native_game_session.h"
#include "render/planar_image.h"
#include "state/race_selection_adapter.h"
#include <algorithm>

namespace scr::runtime {
void NativeGameSession::requestHallBrowser(){
    if(enhancedHallStore_)enhancedHallStore_->cancelTarget();enhancedChallengeRequest_.reset();
    hallResolving_=false;hallBrowser_.loading();hallBrowserRequest_=sharedHallStore_->requestBrowser(hallBrowser_.board());
}
void NativeGameSession::openHallBrowser(const NativeMenuInput& held){
    if(!activeCareer_||multiplayer()||!sharedHallStore_)throw std::logic_error("Hall browser requires an active career");
    const auto track=state::raceSelection::trackId(*data_.memory)%state::raceSelection::trackCount;
    const auto league=activeCareer_->league?HallLeague::Super:HallLeague::Normal;
    HallBoardKey initial{track,league,HallRecordKind::Lap};
    hallBrowser_.open(activeCareer_->name,initial,held);hallBrowserActive_=true;requestHallBrowser();
}
bool NativeGameSession::updateHallBrowser(double elapsed,const NativeGameInput& input,NativeGameUpdate& result){
    if(!hallBrowserActive_)return false;
    if(transitioning()){hallBrowser_.block(input.menu);return false;}
    std::shared_ptr<const EnhancedTargetRead> enhancedRead;
    if(enhancedChallengeRequest_){const auto loaded=enhancedHallStore_->targetRead();if(loaded&&loaded->request==*enhancedChallengeRequest_)enhancedRead=loaded;}
    const auto read=sharedHallStore_->browserRead();
    bool resolved=false;
    if(read&&read->request==hallBrowserRequest_&&read->request!=hallBrowserApplied_){
        hallBrowser_.apply(*read);hallBrowserApplied_=read->request;
        resolved=read->challenge.has_value();
    }
    auto navigation=input.menu;if(hallResolving_)navigation.controls=navigation.numberedKeys=0;
    const auto action=hallBrowser_.update(elapsed,navigation);
    if(enhancedRead&&action!=HallBrowserAction::Back){
        enhancedChallengeRequest_.reset();hallResolving_=false;
        if(enhancedRead->target){startEnhancedHallChallenge(enhancedRead->target,input.menu);return true;}requestHallBrowser();releaseInput(input.menu);
    }
    if(resolved&&action!=HallBrowserAction::Back){
        hallResolving_=false;
        if(read->target){startHallChallenge(read->target,input.menu);return true;}
        releaseInput(input.menu);
    }
    if(action==HallBrowserAction::Load)requestHallBrowser();
    else if(action==HallBrowserAction::Challenge){
        const auto id=hallBrowser_.entries().at(hallBrowser_.selected()).attempt.id;
        hallResolving_=true;hallBrowser_.loading();hallBrowserRequest_=sharedHallStore_->requestBrowser(hallBrowser_.board(),id);
    }
    else if(action==HallBrowserAction::Back){
        enhancedHallStore_->cancelTarget();enhancedChallengeRequest_.reset();
        const auto previous=menuImage();hallBrowserActive_=hallResolving_=false;sharedHallStore_->cancelBrowser();
        menu_->setNavigation(NativeMenuNavigation::Responsive);(void)menu_->back();menu_->setNavigation(navigation_);
        (*data_.memory)[0x5d724]=0x41;
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());fade_.fadeToColor(0);transition_=Transition::OutHallMenu;
        saveCheckpoint();releaseInput(input.menu);
        if(navigation_==NativeMenuNavigation::Responsive)finishResponsiveTransitions(result);
    }
    return true;
}
void NativeGameSession::startHallChallenge(std::shared_ptr<const PinnedHallGhost> target,const NativeMenuInput& held){
    HallChallenge challenge;challenge.target=std::move(target);enterHallChallenge(std::move(challenge),held);
}
void NativeGameSession::enterHallChallenge(HallChallenge challenge,const NativeMenuInput& held){
    const auto previous=menuImage();const auto board=challenge.board();
    auto& c=challenge.continuation;c.memory=std::make_unique<NativeMemory>(*data_.memory);c.menu=menu_->continuation();
    c.suspendedSeason=suspendedSeason_;c.previewPrepared=previewPrepared_;c.preparedTrack=preparedTrack_;c.preparedLeague=preparedLeague_;
    auto& m=*data_.memory;
    state::raceSelection::preparePracticePreview(m,board.track,board.league==HallLeague::Super);
    hallChallenge_=std::move(challenge);hallBrowserActive_=false;sharedHallStore_->cancelBrowser();
    enhancedHallStore_->cancelTarget();enhancedChallengeRequest_.reset();hallResolving_=false;
    race_.setGhostTimingCategory(effectiveTimingCategory());
    NativeMenuContinuation flow;flow.page=NativeMenuPage::Preview;
    menu_=std::make_unique<NativeMenuFlow>(m,flow);menu_->setMainRowCount(3);menu_->setNavigation(navigation_);
    previewPrepared_=false;releaseInput(held);
    transitionIndices_.assign(previous.indices.begin(),previous.indices.end());fade_.fadeToColor(0);transition_=Transition::OutPreview;
}
void NativeGameSession::returnHallChallenge(const NativeMenuInput& held){
    if(!hallChallenge_)throw std::logic_error("No Hall challenge continuation");
    auto c=std::move(hallChallenge_->continuation);hallChallenge_.reset();*data_.memory=*c.memory;
    race_.setGhostTimingCategory(effectiveTimingCategory());
    menu_=std::make_unique<NativeMenuFlow>(*data_.memory,c.menu);menu_->setMainRowCount(3);menu_->setNavigation(navigation_);
    suspendedSeason_=std::move(c.suspendedSeason);previewPrepared_=c.previewPrepared;preparedTrack_=c.preparedTrack;preparedLeague_=c.preparedLeague;
    preparedCompetitive_=c.menu.competitive;lapKey_.reset();ghostNotice_.clear();ghostNoticeSeconds_=0;ghostConditionsDiffer_=false;
    hallBrowserActive_=true;requestHallBrowser();transition_=Transition::None;fade_.reset(readNativePalette(*data_.memory,0x30bd0));
    releaseInput(held);
}
NativeMenuImage NativeGameSession::hallPreviewImage()const{
    NativeMenuImage image;std::copy(preview_.indices().begin(),preview_.indices().end(),image.indices.begin());
    for(unsigned n=0;n<16;++n)image.palette[n]=originalPaletteRgb(std::uint16_t((unsigned((*data_.memory)[0x2b974+n*2])<<8)|(*data_.memory)[0x2b975+n*2]));
    // Keep the original track-name box; replace only its fire-to-continue hint.
    std::fill(image.indices.begin()+188*320,image.indices.end(),0);
    const auto text=[&](const std::string& value,unsigned y){NativeMenuTextCursor cursor;cursor.column=1;cursor.row=(y+8)/8;cursor.yOffset=(y+8)%8;cursor.colour=15;
        for(unsigned char ch:value)drawNativeMenuCharacter(*data_.memory,image,cursor,ch);};
    if(hallChallenge_->enhancedTarget){const auto& rival=hallChallenge_->enhancedTarget->attempt;
        text("Enhanced rival: "+rival.name+" "+formatEnhancedTime(rival.time),191);
    }else{const auto& rival=hallChallenge_->target->attempt;
        text("Hall rival: "+rival.name+" "+formatHallTime(rival.time,HallRecordKind::Lap)+(rival.board.league==HallLeague::Super?" Super":" Normal"),191);}
    return image;
}
} // namespace scr::runtime

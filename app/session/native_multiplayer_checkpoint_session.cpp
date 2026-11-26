#include "session/native_game_session.h"
#include "race/race_transitions.h"

namespace scr::runtime {
NativeMultiplayerCheckpoint NativeGameSession::multiplayerContinuation()const{
    NativeMultiplayerCheckpoint c;c.memory=std::make_unique<NativeMemory>(*data_.memory);c.menu=menu_->continuation();
    c.previewPrepared=previewPrepared_&&(c.menu.page==NativeMenuPage::Matchup||c.menu.page==NativeMenuPage::Preview);
    c.preparedTrack=preparedTrack_;c.preparedLeague=preparedLeague_;
    if(c.menu.page==NativeMenuPage::Results||c.menu.page==NativeMenuPage::Records)c.enhancedRecordNotice=enhancedRecordNotice_;
    if(finishPending_){c.finishPending=true;c.finishPixels=finishPixels_;c.finishTiming=finishTiming_;}
    return c;
}
void NativeGameSession::retainMultiplayerMatchup(){
    // Called before race initialization, after overview preparation. Preserve
    // its exact native state so Resume can reuse preparation in either policy.
    auto c=multiplayerContinuation();c.menu.page=NativeMenuPage::Matchup;(*c.memory)[0x57c62]=0x11;
    c.previewPrepared=true;c.preparedTrack=menu_->trackId();c.preparedLeague=(*data_.memory)[0x1c9d0];
    multiplayerRaceCheckpoint_=encodeNativeMultiplayerCheckpoint(*checkpointBaseline_,c);
}
MultiplayerBytes NativeGameSession::captureMultiplayerCheckpoint(){
    if(!menu_||!multiplayer()||(!menu_->competitive()&&menu_->page()!=NativeMenuPage::Main)||openingActive()||
       careerPanel_!=CareerPanel::None||multiplayerPanel_!=MultiplayerPanel::None||saveMenu_||saveReturn_!=SaveReturn::None||transitioning())throw std::logic_error("Multiplayer checkpoint requires a safe native boundary");
    if(menu_->page()==NativeMenuPage::Race&&!race_.liveTerminal()){
        if(multiplayerRaceCheckpoint_.empty())throw std::logic_error("Multiplayer pre-race checkpoint is unavailable");return multiplayerRaceCheckpoint_;
    }
    auto c=multiplayerContinuation();
    if(menu_->page()==NativeMenuPage::Race){
        race_.copyLiveExit(*c.memory);NativeMenuFlow future(*c.memory,c.menu);future.setNavigation(navigation_);future.returnFromRace();c.menu=future.continuation();
        c.previewPrepared=false;c.enhancedRecordNotice=enhancedRecordNotice_;
        c.finishPending=race_.frames().back().action==unsigned(RaceTransitionAction::raceEnd);
        if(c.finishPending){
            c.finishTiming=race_.enhancedClock(1,false);race_.setGhostTimingCategory(TimingCategory::Original);
            try{c.finishPixels=race_.renderLiveMessage(0x3c,4,0,false,NativeView::Original);}
            catch(...){race_.setGhostTimingCategory(effectiveTimingCategory());throw;}
            race_.setGhostTimingCategory(effectiveTimingCategory());
        }
    }else if(c.menu.page==NativeMenuPage::Preview){c.menu.page=NativeMenuPage::Matchup;(*c.memory)[0x57c62]=0x11;}
    auto bytes=encodeNativeMultiplayerCheckpoint(*checkpointBaseline_,c);multiplayerCheckpointEnabled_=true;return bytes;
}
void NativeGameSession::restoreMultiplayerCheckpoint(const MultiplayerBytes& bytes,const NativeMenuInput& held,bool allowUnsaved){
    auto c=decodeNativeMultiplayerCheckpoint(*checkpointBaseline_,bytes);
    auto restoredMenu=std::make_unique<NativeMenuFlow>(*data_.memory,c.menu);restoredMenu->setNavigation(navigation_);
    if((careerStore_&&!flushStores()&&!allowUnsaved))throw std::runtime_error("Finish saving before restoring a multiplayer game");
    // Everything that can reject the data has completed. Keep the memory object
    // itself alive because native input/name adapters hold references to it.
    *data_.memory=*c.memory;menu_=std::move(restoredMenu);opening_.reset();nameScreen_.reset();saveMenu_.reset();saveReturn_=SaveReturn::None;pendingSave_.reset();
    activeCareer_.reset();newCareer_.reset();checkpoint_.clear();newCheckpoint_.clear();suspendedSeason_.clear();careerDraft_.reset();
    careerPanel_=CareerPanel::None;departure_=Departure::None;closeReady_=false;creatingCareer_=false;multiplayerSetup_=true;
    multiplayerPanel_=MultiplayerPanel::None;
    previewPrepared_=c.previewPrepared;preparedTrack_=c.preparedTrack;preparedLeague_=c.preparedLeague;preparedCompetitive_=true;
    finishPending_=c.finishPending;finishPixels_=std::move(c.finishPixels);finishTiming_=std::move(c.finishTiming);enhancedRecordNotice_=std::move(c.enhancedRecordNotice);
    enhancedLapRecord_.reset();enhancedRaceRecord_.reset();hallChallenge_.reset();hallBrowserActive_=false;hallResolving_=false;enhancedChallengeRequest_.reset();
    lapKey_.reset();ghostNotice_.clear();ghostNoticeSeconds_=0;driveClock_.reset();endFireWait_.reset();transition_=Transition::None;
    fade_.reset(readNativePalette(*data_.memory,0x23c32));multiplayerRaceCheckpoint_=bytes;multiplayerCheckpointEnabled_=true;releaseInput(held);
}
bool NativeGameSession::updateMultiplayerFinish(double elapsed,const NativeGameInput& held,NativeGameUpdate& result){
    if(!finishPending_||!multiplayer()||careerPanel_!=CareerPanel::None||multiplayerPanel_!=MultiplayerPanel::None)return false;
    const auto pulse=careerClock_.sample(held.menu,elapsed);
    if(pulse.controls&16){finishPending_=false;releaseInput(held.menu);result.raceExited=true;}
    return true;
}
} // namespace scr::runtime

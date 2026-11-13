#include "session/native_game_session.h"
#include "state/race_selection_adapter.h"
#include <limits>

namespace scr::runtime {
TimingCategory NativeGameSession::effectiveTimingCategory()const noexcept{
    if(hallChallenge_)return hallChallenge_->enhancedTarget?TimingCategory::Enhanced:TimingCategory::Original;
    return timingCategory_;
}
void NativeGameSession::setTimingCategory(TimingCategory category){
    if(category!=TimingCategory::Original&&category!=TimingCategory::Enhanced)throw std::invalid_argument("Invalid timing category");
    const auto before=effectiveTimingCategory();timingCategory_=category;race_.setGhostTimingCategory(effectiveTimingCategory());
    if(before!=effectiveTimingCategory()){ghostNotice_.clear();ghostNoticeSeconds_=0;ghostConditionsDiffer_=false;}
}
void NativeGameSession::prepareEnhancedRecords(){
    enhancedImprovements_=0;enhancedBestFileState_=CareerFileState::Missing;
    if(!lapKey_||!enhancedBests_)return;
    const EnhancedBestKey key{lapKey_->career,lapKey_->track,lapKey_->superLeague};
    const auto saved=careerStore_->readEnhancedBest(key,enhancedBests_->catalog());enhancedBestFileState_=saved.state;
    auto diskTime=std::numeric_limits<std::int64_t>::max();
    if(saved.state==CareerFileState::Valid||saved.state==CareerFileState::Recovered){
        auto best=decodeEnhancedBest(saved.data,key,enhancedBests_->catalog());diskTime=best.time.milliseconds();enhancedBests_->offer(std::move(best));
    }else if(effectiveTimingCategory()==TimingCategory::Enhanced&&(saved.state==CareerFileState::Unsupported||saved.state==CareerFileState::Corrupt)){
        ghostNotice_="Saved Enhanced PB unavailable. Clear it in the Practice game menu.";ghostNoticeSeconds_=5;
    }
    const auto retained=enhancedBests_->best(key);
    if(retained&&(saved.state!=CareerFileState::Valid||retained->time.milliseconds()<diskTime))careerStore_->saveEnhancedBest(*retained);
    race_.recordEnhancedPersonalBestLaps(*enhancedBests_,key,hallChallenge_?hallChallenge_->enhancedTarget:nullptr);
}
void NativeGameSession::prepareEnhancedRecordDisplay(){
    enhancedRecordNotice_.reset();enhancedLapRecord_.reset();enhancedRaceRecord_.reset();
    std::optional<EnhancedTime> lap,race;
    if(lapKey_&&enhancedBests_)if(const auto pb=enhancedBests_->best({lapKey_->career,lapKey_->track,lapKey_->superLeague}))lap=pb->time;
    if(enhancedHallStore_)try{const auto saved=enhancedHallStore_->read();if(saved.hall){
        HallBoardKey key{menu_->trackId(),state::raceSelection::superLeague(*data_.memory)?HallLeague::Super:HallLeague::Normal,HallRecordKind::Lap};
        enhancedRecordNotice_.emplace();
        const auto& laps=saved.hall->entries(key);if(!laps.empty()){
            enhancedLapRecord_=laps.front().attempt.time.milliseconds();
            if(!lap||*enhancedLapRecord_<lap->milliseconds())lap=laps.front().attempt.time;
        }
        key.kind=HallRecordKind::Race;const auto& races=saved.hall->entries(key);if(!races.empty()){
            race=races.front().attempt.time;enhancedRaceRecord_=race->milliseconds();
        }
    }}catch(const std::exception&){/* Unavailable records have no invented precision. */}
    race_.setEnhancedRecordTargets(std::move(lap),std::move(race));
}
void NativeGameSession::observeEnhancedRecord(const EnhancedHallAttempt& attempt){
    if(!enhancedRecordNotice_||!attempt.time.rankable())return;
    const auto milliseconds=attempt.time.milliseconds();
    auto& best=attempt.board.kind==HallRecordKind::Lap?enhancedLapRecord_:enhancedRaceRecord_;
    auto& line=attempt.board.kind==HallRecordKind::Lap?enhancedRecordNotice_->lap:enhancedRecordNotice_->race;
    if(!best||milliseconds<*best){best=milliseconds;line=EnhancedRecordLine{attempt.name,milliseconds};}
}
void NativeGameSession::observeEnhancedPersonalBest(){
    if(!lapKey_||!enhancedBests_)return;
    const auto* stats=race_.lapRecordingStatistics();if(!stats||stats->enhancedImprovements==enhancedImprovements_)return;
    enhancedImprovements_=stats->enhancedImprovements;
    const auto best=enhancedBests_->best({lapKey_->career,lapKey_->track,lapKey_->superLeague});if(!best)return;
    careerStore_->saveEnhancedBest(*best);
    if(effectiveTimingCategory()==TimingCategory::Enhanced){ghostNotice_="New Enhanced personal best: "+formatEnhancedTime(best->time);ghostNoticeSeconds_=2.5;}
}
EnhancedHallRead NativeGameSession::enhancedHall()const{
    if(!enhancedHallStore_)throw std::logic_error("Enhanced Hall is not enabled");return enhancedHallStore_->read();
}
void NativeGameSession::requestEnhancedHallChallenge(const EnhancedHallAttempt& attempt){
    if(!activeCareer_||multiplayer()||!hallBrowserActive_||page()!=NativeMenuPage::HallOfFame||hallResolving_||hallChallenge_)
        throw std::logic_error("Enhanced challenge requires an idle Hall browser");
    enhancedChallengeRequest_=enhancedHallStore_->requestTarget(attempt);hallResolving_=true;hallBrowser_.loading();
}
void NativeGameSession::startEnhancedHallChallenge(std::shared_ptr<const PinnedEnhancedHallGhost> target,const NativeMenuInput& held){
    if(!target||!activeCareer_||multiplayer()||!hallBrowserActive_||page()!=NativeMenuPage::HallOfFame||hallChallenge_||
       !sameEnhancedCatalog(target->catalog,enhancedBests_->catalog())||target->attempt.board.kind!=HallRecordKind::Lap||
       !sameEnhancedTime(target->attempt.time,target->recording.time))throw std::invalid_argument("Enhanced challenge differs from session context");
    (void)EnhancedHallOfFame(target->catalog).offer(target->attempt);validateEnhancedRecording(target->recording,target->attempt.board.track);
    HallChallenge challenge;challenge.enhancedTarget=std::move(target);enterHallChallenge(std::move(challenge),held);
}
} // namespace scr::runtime

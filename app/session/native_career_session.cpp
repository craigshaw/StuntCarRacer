#include "session/native_game_session.h"
#include "presentation/native_hall_of_fame.h"
#include "presentation/native_competition_render.h"
#include "render/planar_image.h"
#include "race/race_transitions.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace scr::runtime {
namespace {
std::uint64_t now(){static std::uint64_t previous{};const auto clock=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());previous=std::max(clock,previous+1);return previous;}
std::string driverName(const NativeMemory& m){
    std::string name(m.begin()+0x1ecab+11*16,m.begin()+0x1ecab+11*16+12);
    while(!name.empty()&&name.back()==' ')name.pop_back();return name;
}
std::string description(const NativeCareerInfo& info){
    if(info.state==CareerFileState::Corrupt||info.state==CareerFileState::Missing)return "Damaged - cannot continue";
    if(info.state==CareerFileState::Unsupported)return "Unsupported save version";
    const auto stamp=std::time_t(info.lastPlayed/1000);std::tm time{};localtime_s(&time,&stamp);
    std::ostringstream text;text<<"D"<<4-info.division<<(info.league?" Super ":" Normal ")<<std::put_time(&time,"%d/%m/%y %H:%M");return text.str();
}
}
void NativeGameSession::enableCareers(const std::filesystem::path& root,const std::filesystem::path& sharedRoot){
    if(careerStore_||!openingActive()||opening_->page()!=NativeOpeningPage::Mode)throw std::logic_error("Careers must start at the mode chooser");
    careerBaseline_=std::make_unique<NativeMemory>(*data_.memory);careerStore_=std::make_unique<NativeCareerStore>(root,[this](const CareerBytes& data){
        try{CareerBytes bytes;(void)decodeCareerInfo(data,&bytes);(void)decodeNativeCheckpoint(*careerBaseline_,bytes);return CareerFileState::Valid;}
        catch(const UnsupportedNativeCheckpoint&){return CareerFileState::Unsupported;}
        catch(...){return CareerFileState::Corrupt;}
    });
    lastCareer_=careerStore_->lastUsed();
    sharedHallStore_=std::make_unique<SharedHallStore>(sharedRoot.empty()?root/"shared-hall":sharedRoot,
        HallCatalog{ghostTrackContentIdentity(data_.payload),1},HallWriteObserver{},hallCapturePendingMaximumBytes);
    sharedHallStore_->retry(); // Check/open off-thread; a failure cannot block career loading.
    if(careerPanel_==CareerPanel::None&&careerStore_->lastUsedState()==CareerFileState::Recovered){
        notice_="Last career recovery restored";returnPanel_=CareerPanel::None;showCareerPanel(CareerPanel::Notice);
    }
}
std::string NativeGameSession::saveStatus()const{
    if(!careerStore_)return {};
    if(storesFailed())return "NOT SAVED";
    return careerStore_->pending()||(sharedHallStore_&&sharedHallStore_->pending())?"Saving...":"Saved";
}
void NativeGameSession::refreshCareerHome(){
    careerList_=careerStore_->list();homeActions_.clear();recentCareerName_.clear();
    // The newest metadata can still be queued for disk. Keep browsing and
    // last-played ordering consistent with the career already open in RAM.
    if(activeCareer_&&!checkpoint_.empty()){
        const auto current=std::find_if(careerList_.begin(),careerList_.end(),[&](const auto& item){return item.id==activeCareer_->id;});
        if(current==careerList_.end())careerList_.push_back(*activeCareer_);else *current=*activeCareer_;
        std::sort(careerList_.begin(),careerList_.end(),[](const auto& a,const auto& b){return a.lastPlayed!=b.lastPlayed?a.lastPlayed>b.lastPlayed:a.id<b.id;});
    }
    if(!lastCareer_.empty()){
        if(activeCareer_&&activeCareer_->id==lastCareer_&&!checkpoint_.empty())recentCareerName_=activeCareer_->name;
        else for(const auto& career:careerList_)if(career.id==lastCareer_&&(career.state==CareerFileState::Valid||career.state==CareerFileState::Recovered)){recentCareerName_=career.name;break;}
    }
    if(!recentCareerName_.empty())homeActions_.push_back(HomeAction::Continue);
    homeActions_.push_back(HomeAction::NewCareer);
    if(!careerList_.empty())homeActions_.push_back(HomeAction::Browse);
}
void NativeGameSession::showCareerPanel(CareerPanel panel){
    if(panel==CareerPanel::Home)refreshCareerHome();
    careerPanel_=panel;careerSelection_=0;careerClock_.clear();releaseInput();
}
void NativeGameSession::showCareerHome(){
    if(activeCareer_&&!openingActive()&&careerPanel_==CareerPanel::None)saveCheckpoint(raceEnded());
    showCareerPanel(CareerPanel::Home);
}
void NativeGameSession::openSinglePlayerMenu(bool captureSeason){
    if(!activeCareer_||!menu_)throw std::logic_error("No open single-player career");
    if(menu_->competitive()||finishPending_){
        if(captureSeason)saveCheckpoint(raceEnded());
        const auto season=checkpoint_;
        // A terminal checkpoint already contains its scored outcome. Use that
        // state for the profile menu without applying exit/scoring to live RAM.
        restoreCheckpoint(season);suspendedSeason_=season;
    }
    opening_.reset();menu_=std::make_unique<NativeMenuFlow>(*data_.memory);menu_->setMainRowCount(3);menu_->setNavigation(navigation_);
    finishPending_=false;finishPixels_.clear();previewPrepared_=false;transition_=Transition::None;fade_.reset(readNativePalette(*data_.memory,0x23c32));
    careerPanel_=CareerPanel::None;releaseInput();saveCheckpoint();
}
void NativeGameSession::resumeSeason(){
    const auto season=suspendedSeason_;
    if(season.empty())throw std::logic_error("No suspended season");
    restoreCheckpoint(season);checkpoint_=season;
    careerStore_->retry();careerStore_->saveCareer(*activeCareer_,checkpoint_);
}
NativeMenuImage NativeGameSession::careerImage()const{
    std::string heading="Single Player",detail;std::vector<std::string> rows;
    unsigned selected=careerSelection_;
    switch(careerPanel_){
    case CareerPanel::Home:
        for(const auto action:homeActions_)rows.push_back(action==HomeAction::Continue?"Continue":action==HomeAction::NewCareer?"New Career":"Browse Careers");
        detail=recentCareerName_;break;
    case CareerPanel::List:{
        heading="Browse Careers";const unsigned first=(listSelection_/3)*3;
        for(unsigned n=first;n<careerList_.size()&&n<first+3;++n)rows.push_back(careerList_[n].name);
        if(rows.empty()){rows={"Back"};detail="No careers";selected=0;}
        else{selected=listSelection_-first;detail=description(careerList_[listSelection_]);}
        break;}
    case CareerPanel::Actions:heading=careerList_[listSelection_].name;detail=description(careerList_[listSelection_]);rows={"Continue","Delete Career","Back"};break;
    case CareerPanel::Delete:heading="Delete "+careerList_[listSelection_].name+"?";detail="Career and recovery copies";rows={"Cancel","Delete permanently"};break;
    case CareerPanel::Notice:heading="Career notice";detail=notice_;rows={"OK"};break;
    case CareerPanel::SaveError:heading=storeFailureSummary();detail="Check storage and permissions";rows={"Retry","Continue without saving"};break;
    case CareerPanel::CreateError:heading="Career not created";detail="A first save is required";rows={"Retry","Cancel"};break;
    case CareerPanel::Departure:heading=storeFailureSummary();detail="Leave loses unsaved changes";rows={"Retry","Leave unsaved","Cancel"};break;
    default:rows={"Back"};break;
    }
    return makeNativeChoiceImage(*data_.memory,heading,rows,selected,detail,saveStatus());
}
void NativeGameSession::beginCareer(){
    *data_.memory=careerDraft_?*careerDraft_:*careerBaseline_;menu_.reset();opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);
    opening_->setNavigation(navigation_);nameLengths_.fill(0);if(careerDraft_)nameLengths_[0]=draftNameLength_;creatingCareer_=true;finishPending_=false;previewPrepared_=false;suspendedSeason_.clear();
    careerPanel_=CareerPanel::None;transition_=Transition::None;fade_.reset(readNativePalette(*data_.memory,0x23c32));NativeGameUpdate ignored;
    handleOpeningAction(opening_->beginSingleName(),makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]),ignored);
    releaseInput();
}
void NativeGameSession::registerCareer(){
    menu_->setMainRowCount(3);
    newCareer_=NativeCareerInfo{NativeCareerStore::newId(),driverName(*data_.memory),(*data_.memory)[0x1c9ce],(*data_.memory)[0x1c9d0],now()};
    NativeCareerCheckpoint checkpoint;checkpoint.memory=std::make_unique<NativeMemory>(*data_.memory);checkpoint.menu=menu_->continuation();
    newCheckpoint_=encodeNativeCheckpoint(*careerBaseline_,checkpoint);
    careerStore_->saveCareer(*newCareer_,newCheckpoint_);
    completeCareerCreation();
}
void NativeGameSession::completeCareerCreation(){
    fade_.reset(readNativePalette(*data_.memory,0x23c32));
    if(!careerStore_->flush()){transition_=Transition::None;showCareerPanel(CareerPanel::CreateError);return;}
    careerStore_->setLastUsed(newCareer_->id);
    if(!careerStore_->flush()){transition_=Transition::None;showCareerPanel(CareerPanel::CreateError);return;}
    activeCareer_=newCareer_;lastCareer_=activeCareer_->id;checkpoint_=std::move(newCheckpoint_);newCareer_.reset();creatingCareer_=false;careerDraft_.reset();
    careerPanel_=CareerPanel::None;transition_=Transition::None;releaseInput();
}
void NativeGameSession::cancelCareerCreation(){
    careerDraft_=std::make_unique<NativeMemory>(*data_.memory);draftNameLength_=unsigned(driverName(*data_.memory).size());
    if(newCareer_)(void)careerStore_->removeCareer(newCareer_->id);newCareer_.reset();newCheckpoint_.clear();creatingCareer_=false;
    careerStore_->setLastUsed(lastCareer_);(void)careerStore_->flush();showCareerPanel(CareerPanel::Home);
}
void NativeGameSession::saveCheckpoint(bool terminal){
    if(!activeCareer_||creatingCareer_||!menu_||(*data_.memory)[0x5eb76]||openingActive())return;
    // A challenge is a temporary world. Persist the career's safe continuation,
    // never its temporary league, preview RNG or Practice race state.
    if(hallChallenge_){careerStore_->saveCareer(*activeCareer_,checkpoint_);return;}
    if(finishPending_){careerStore_->saveCareer(*activeCareer_,checkpoint_);return;}
    NativeCareerCheckpoint c;c.memory=std::make_unique<NativeMemory>(*data_.memory);c.menu=menu_->continuation();
    c.suspendedSeason=suspendedSeason_;
    c.previewPrepared=previewPrepared_;c.preparedTrack=preparedTrack_;c.preparedLeague=preparedLeague_;
    if(terminal&&menu_->competitive()){
        race_.copyLiveExit(*c.memory);NativeMenuFlow future(*c.memory,c.menu);future.setNavigation(navigation_);future.returnFromRace();c.menu=future.continuation();
        c.finishPending=race_.frames().back().action==unsigned(RaceTransitionAction::raceEnd);
        if(c.finishPending){race_.renderLive(0,false,false,NativeView::Original);c.finishPixels=race_.renderLiveMessage(0x3c,4);}
        c.previewPrepared=false;
    }else if(c.menu.page==NativeMenuPage::Race)return; // retain the pre-race fixture
    else if(c.menu.page==NativeMenuPage::Preview&&c.menu.competitive){c.menu.page=NativeMenuPage::Matchup;(*c.memory)[0x57c62]=0x11;}
    else if(!c.menu.competitive&&c.menu.page!=NativeMenuPage::Main){
        // Practice resumes at the career menu; preserve its resulting RNG and
        // all career fields. No Practice race or selection journey is resumed.
        NativeMenuFlow main(*c.memory);main.setMainRowCount(3);main.setNavigation(navigation_);c.menu=main.continuation();c.previewPrepared=false;
    }else if(c.menu.page==NativeMenuPage::HallOfFame){NativeMenuFlow main(*c.memory);main.setMainRowCount(3);c.menu=main.continuation();}
    checkpoint_=encodeNativeCheckpoint(*careerBaseline_,c);
    activeCareer_->division=(*c.memory)[0x1c9ce];activeCareer_->league=(*c.memory)[0x1c9d0];
    careerStore_->retry();careerStore_->saveCareer(*activeCareer_,checkpoint_);
}
void NativeGameSession::restoreCheckpoint(const CareerBytes& bytes){
    auto c=decodeNativeCheckpoint(*careerBaseline_,bytes);*data_.memory=*c.memory;
    opening_.reset();saveMenu_.reset();saveReturn_=SaveReturn::None;menu_=std::make_unique<NativeMenuFlow>(*data_.memory,c.menu);menu_->setMainRowCount(3);menu_->setNavigation(navigation_);
    suspendedSeason_=std::move(c.suspendedSeason);
    previewPrepared_=c.previewPrepared;preparedTrack_=c.preparedTrack;preparedLeague_=c.preparedLeague;preparedCompetitive_=c.menu.competitive;
    finishPending_=c.finishPending;finishPixels_=std::move(c.finishPixels);transition_=Transition::None;fade_.reset(readNativePalette(*data_.memory,0x23c32));careerPanel_=CareerPanel::None;creatingCareer_=false;releaseInput();
}
bool NativeGameSession::loadCareer(const std::string& id){
    try{
        CareerBytes bytes;NativeCareerInfo info;CareerFileState state=CareerFileState::Valid;
        if(activeCareer_&&activeCareer_->id==id&&!checkpoint_.empty()){bytes=checkpoint_;info=*activeCareer_;}
        else{
            const auto saved=careerStore_->readCareer(id);state=saved.state;
            if(state!=CareerFileState::Valid&&state!=CareerFileState::Recovered)throw std::runtime_error(state==CareerFileState::Unsupported?"Unsupported career version":"Career is unavailable");
            info=decodeCareerInfo(saved.data,&bytes);if(info.id!=id)throw std::runtime_error("Career identity mismatch");
        }
        restoreCheckpoint(bytes);activeCareer_=info;checkpoint_=std::move(bytes);activeCareer_->lastPlayed=now();lastCareer_=id;
        openSinglePlayerMenu(false);careerStore_->setLastUsed(id);
        if(state==CareerFileState::Recovered){notice_="Career recovery copy restored";returnPanel_=CareerPanel::None;showCareerPanel(CareerPanel::Notice);}
        return true;
    }catch(const std::exception& error){notice_=error.what();returnPanel_=CareerPanel::Home;showCareerPanel(CareerPanel::Notice);return false;}
}
bool NativeGameSession::depart(Departure action,const std::string& id){
    if(action==Departure::LoadCareer&&activeCareer_&&id==activeCareer_->id&&!checkpoint_.empty())return loadCareer(id);
    departure_=action;loadCareer_=id;departureReturnPanel_=careerPanel_;
    if(!flushStores(true)){showCareerPanel(CareerPanel::Departure);return false;}
    completeDeparture();return true;
}
void NativeGameSession::completeDeparture(){
    const auto action=departure_;departure_=Departure::None;
    if(action==Departure::Close){closeReady_=true;return;}
    if(action==Departure::NewCareer){beginCareer();return;}
    if(action==Departure::LoadCareer){(void)loadCareer(loadCareer_);return;}
    if(action==Departure::Multiplayer){
        *data_.memory=*careerBaseline_;menu_.reset();opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);
        opening_->setNavigation(navigation_);multiplayerSetup_=true;creatingCareer_=false;finishPending_=false;previewPrepared_=false;suspendedSeason_.clear();
        careerPanel_=CareerPanel::None;transition_=Transition::None;fade_.reset(readNativePalette(*data_.memory,0x23c32));nameLengths_.fill(0);NativeGameUpdate ignored;
        handleOpeningAction(opening_->beginMultiplayerName(),makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]),ignored);releaseInput();
    }
}
bool NativeGameSession::requestClose(){
    if(!careerStore_)return true;
    if(careerPanel_==CareerPanel::Departure)return false;
    if(!transitioning()&&careerPanel_==CareerPanel::None&&!openingActive())saveCheckpoint(raceEnded());
    (void)depart(Departure::Close);return closeReady_;
}
void NativeGameSession::observeHallCompletions(){
    if(!sharedHallStore_)return;
    for(const auto& completed:race_.takeHallCompletions()){
        // The live budget fits every possible pending top-20 capture. Rejection
        // as dominated is safe; Full would indicate a broken capture bound.
        if(sharedHallStore_->submit(completed)==HallSubmitResult::Full)throw std::logic_error("Hall capture allocation bound exceeded");
    }
}
HallArchiveRead NativeGameSession::sharedHall()const{
    if(!sharedHallStore_)throw std::logic_error("Shared Hall is not enabled");return sharedHallStore_->read();
}
bool NativeGameSession::storesFailed()const{return (careerStore_&&careerStore_->failed())||(sharedHallStore_&&sharedHallStore_->failed());}
std::string NativeGameSession::storeFailureSummary()const{
    if(sharedHallStore_&&sharedHallStore_->failed())return careerStore_&&careerStore_->failed()?"Progress and Hall not saved":"Hall records not saved";
    return careerStore_?careerStore_->failureSummary():std::string{};
}
bool NativeGameSession::flushStores(bool retry){
    if(retry){if(careerStore_)careerStore_->retry();if(sharedHallStore_)sharedHallStore_->retry();}
    const bool a=!careerStore_||careerStore_->flush();const bool b=!sharedHallStore_||sharedHallStore_->flush();return a&&b;
}
bool NativeGameSession::flushRecords(){return flushStores();}
void NativeGameSession::observeCareerState(NativeMenuPage previous){
    if(!careerStore_)return;
    if(careerPanel_!=CareerPanel::None||openingActive()||!menu_)return;
    if(activeCareer_&&!(*data_.memory)[0x5eb76])menu_->setMainRowCount(3);
    if(page()!=previous&&page()!=NativeMenuPage::Race)saveCheckpoint();
    checkSaveFailure();
}
void NativeGameSession::checkSaveFailure(){
    if(storesFailed()&&!errorAcknowledged_&&!transitioning()&&careerPanel_==CareerPanel::None&&
       !(page()==NativeMenuPage::Race&&!raceEnded()&&race_.liveTicks()!=0)&&!creatingCareer_){returnPanel_=CareerPanel::None;showCareerPanel(CareerPanel::SaveError);}
    if(!storesFailed())errorAcknowledged_=false;
}
bool NativeGameSession::updateCareers(double elapsed,const NativeGameInput& held,NativeGameUpdate& result){
    if(!careerStore_)return false;
    if(!storesFailed())errorAcknowledged_=false;
    if(storesFailed()&&!errorAcknowledged_&&(careerPanel_==CareerPanel::Home||careerPanel_==CareerPanel::List||careerPanel_==CareerPanel::Actions)){
        returnPanel_=careerPanel_;showCareerPanel(CareerPanel::SaveError);careerClock_.enterScreen(held.menu);return true;
    }
    if(careerPanel_!=CareerPanel::None){
        const auto input=careerClock_.sample(held.menu,elapsed);unsigned count=1;
        switch(careerPanel_){
        case CareerPanel::Home:count=unsigned(homeActions_.size());break;
        case CareerPanel::Actions:case CareerPanel::Departure:count=3;break;
        case CareerPanel::Delete:case CareerPanel::SaveError:case CareerPanel::CreateError:count=2;break;
        case CareerPanel::List:count=unsigned(careerList_.size());break;default:break;}
        if(careerPanel_==CareerPanel::List){
            if((input.controls&1)&&listSelection_)--listSelection_;
            if((input.controls&2)&&listSelection_+1<count)++listSelection_;
            if((input.controls&4)&&listSelection_>=3)listSelection_-=3;
            if((input.controls&8)&&count)listSelection_=std::min(listSelection_+3,count-1);
        }else{
            if((input.controls&1)&&careerSelection_)--careerSelection_;
            if((input.controls&2)&&careerSelection_+1<count)++careerSelection_;
            for(unsigned n=0;n<count&&n<4;++n)if(input.numberedKeys&(1u<<n))careerSelection_=n;
        }
        if(input.back){
            switch(careerPanel_){
            case CareerPanel::Home:
                careerPanel_=CareerPanel::None;opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);opening_->setNavigation(navigation_);transition_=Transition::None;multiplayerSetup_=false;break;
            case CareerPanel::List:showCareerPanel(CareerPanel::Home);break;
            case CareerPanel::Actions:showCareerPanel(CareerPanel::List);break;
            case CareerPanel::Delete:showCareerPanel(CareerPanel::Actions);break;
            case CareerPanel::CreateError:cancelCareerCreation();break;
            case CareerPanel::SaveError:errorAcknowledged_=true;showCareerPanel(returnPanel_);break;
            case CareerPanel::Departure:departure_=Departure::None;showCareerPanel(departureReturnPanel_);break;
            case CareerPanel::Notice:showCareerPanel(returnPanel_);break;
            default:break;
            }
            careerClock_.enterScreen(held.menu);navigationClock_.enterScreen(held.menu);return true;
        }
        if(!(input.controls&16))return true;
        const auto choice=careerSelection_;
        switch(careerPanel_){
        case CareerPanel::Home:
            switch(homeActions_.at(choice)){
            case HomeAction::Continue:(void)depart(Departure::LoadCareer,lastCareer_);break;
            case HomeAction::NewCareer:(void)depart(Departure::NewCareer);break;
            case HomeAction::Browse:refreshCareerHome();listSelection_=0;showCareerPanel(CareerPanel::List);break;
            }break;
        case CareerPanel::List:if(!careerList_.empty())showCareerPanel(CareerPanel::Actions);else showCareerPanel(CareerPanel::Home);break;
        case CareerPanel::Actions:
            if(choice==0)(void)depart(Departure::LoadCareer,careerList_[listSelection_].id);
            if(choice==1)showCareerPanel(CareerPanel::Delete);
            if(choice==2)showCareerPanel(CareerPanel::List);break;
        case CareerPanel::Delete:
            if(choice==0)showCareerPanel(CareerPanel::Actions);
            else{const auto id=careerList_[listSelection_].id;
                if(careerStore_->removeCareer(id)){
                    personalBestLaps_.forgetCareer(id);
                    if(activeCareer_&&activeCareer_->id==id){activeCareer_.reset();checkpoint_.clear();suspendedSeason_.clear();}
                    if(lastCareer_==id)lastCareer_.clear();refreshCareerHome();listSelection_=0;showCareerPanel(CareerPanel::List);
                }else{notice_="Could not delete career";returnPanel_=CareerPanel::List;showCareerPanel(CareerPanel::Notice);}}
            break;
        case CareerPanel::Notice:showCareerPanel(returnPanel_);break;
        case CareerPanel::SaveError:
            if(choice==0){if(flushStores(true))showCareerPanel(returnPanel_);}
            else{errorAcknowledged_=true;showCareerPanel(returnPanel_);}break;
        case CareerPanel::CreateError:
            if(choice==0){careerStore_->retry();completeCareerCreation();}
            else cancelCareerCreation();break;
        case CareerPanel::Departure:
            if(choice==0){if(flushStores(true))completeDeparture();}
            else if(choice==1){careerStore_->acknowledgeFailures();errorAcknowledged_=true;completeDeparture();}
            else{departure_=Departure::None;showCareerPanel(departureReturnPanel_);}break;
        default:break;
        }
        careerClock_.enterScreen(held.menu);navigationClock_.enterScreen(held.menu);return true;
    }
    // Modern Back is independent of the timing policy. Let the original drive
    // loop retain Escape/View's deliberate Leave Race behaviour while moving.
    if(hallBrowserActive_)return updateHallBrowser(elapsed,held,result);
    if(transitioning())return false;
    const auto pulse=careerClock_.sample(held.menu,elapsed);
    if(creatingCareer_&&enteringName()&&pulse.back){
        careerDraft_=std::make_unique<NativeMemory>(*data_.memory);draftNameLength_=nameEntry_.state().position;creatingCareer_=false;
        showCareerPanel(CareerPanel::Home);releaseInput(held.menu);return true;
    }
    if(finishPending_){
        if(pulse.back)openSinglePlayerMenu();
        else if(pulse.controls&16){finishPending_=false;saveCheckpoint();}
        if(pulse.back||(pulse.controls&16)){releaseInput(held.menu);result.raceExited=true;}
        return true;
    }
    if(activeCareer_&&!openingActive()&&menu_&&!(*data_.memory)[0x5eb76]){
        const auto current=page();
        if(pulse.back){
            if(hallChallenge_&&current==NativeMenuPage::Preview){returnHallChallenge(held.menu);return true;}
            if(current==NativeMenuPage::Preview||current==NativeMenuPage::Division||current==NativeMenuPage::Track||current==NativeMenuPage::HallOfFame){
                menu_->setNavigation(NativeMenuNavigation::Responsive);const auto action=menu_->back();menu_->setNavigation(navigation_);
                if(action==NativeMenuAction::LeaveHallOfFame)(*data_.memory)[0x5d724]=0x41;
                saveCheckpoint();releaseInput(held.menu);return true;
            }
            if(current!=NativeMenuPage::Race||raceEnded()){
                if(menu_->competitive())openSinglePlayerMenu();else showCareerHome();
                releaseInput(held.menu);return true;
            }
        }
        if(current==NativeMenuPage::Main&&(pulse.controls&16)&&(*data_.memory)[0x1bbd6]==2&&!suspendedSeason_.empty()){
            resumeSeason();releaseInput(held.menu);return true;
        }
    }
    return false;
}
} // namespace scr::runtime

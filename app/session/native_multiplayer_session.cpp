#include "session/native_game_session.h"
#include "presentation/native_multiplayer_render.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace scr::runtime {
namespace {
std::uint64_t playedNow(){static std::uint64_t previous{};const auto clock=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());return previous=std::max(clock,previous+1);}
std::string summary(const SavedGameEntry& entry){
    if(entry.state==SavedGameFileState::Corrupt||entry.state==SavedGameFileState::Missing)return "Damaged - cannot continue";
    if(entry.state==SavedGameFileState::Unsupported)return "Unsupported save version";
    return std::to_string(entry.players)+"P D"+std::to_string(4-entry.division)+(entry.league?" Super C":" Normal C")+std::to_string(entry.championship)+
        (entry.competition?" R"+std::to_string(entry.race)+"/"+std::to_string(2*entry.players)+(entry.finish?" finish":entry.report?" report":" ready"):" ready");
}
std::string folderLabel(const std::filesystem::path& path){const auto name=path.filename().u8string();return name.size()>25?name.substr(0,11)+"..."+name.substr(name.size()-11):name;}
bool usable(SavedGameFileState state){return state==SavedGameFileState::Valid||state==SavedGameFileState::Recovered;}
}
void NativeGameSession::enableMultiplayerGames(const std::filesystem::path& settings){
    if(!careerStore_||multiplayerStore_||!opening_||opening_->page()!=NativeOpeningPage::Mode)throw std::logic_error("Saved multiplayer starts at the mode chooser");
    multiplayerStore_=std::make_unique<NativeMultiplayerStore>(*careerStore_,*checkpointBaseline_,settings);lastMultiplayer_=multiplayerStore_->lastUsed();
}
void NativeGameSession::refreshMultiplayer(){
    multiplayerList_=multiplayerStore_->list();multiplayerActions_.clear();bool available=false;
    for(const auto& entry:multiplayerList_)if(entry.id==lastMultiplayer_&&usable(entry.state))available=true;
    if(activeMultiplayer_&&activeMultiplayer_->id==lastMultiplayer_&&!multiplayerCheckpoint_.empty()){
        available=std::none_of(multiplayerList_.begin(),multiplayerList_.end(),[&](const auto& entry){return entry.id==lastMultiplayer_&&entry.state==SavedGameFileState::Conflict;});
    }
    if(available)multiplayerActions_.push_back(HomeAction::Continue);multiplayerActions_.push_back(HomeAction::NewCareer);
    if(!multiplayerList_.empty())multiplayerActions_.push_back(HomeAction::Browse);if(multiplayerListChoice_>=multiplayerList_.size())multiplayerListChoice_=0;
}
void NativeGameSession::showMultiplayerPanel(MultiplayerPanel panel){
    if(panel==MultiplayerPanel::Select||panel==MultiplayerPanel::List)refreshMultiplayer();
    multiplayerPanel_=panel;multiplayerChoice_=panel==MultiplayerPanel::Conflict?2u:0u;releaseInput();
    if(panel==MultiplayerPanel::Title){multiplayerTextReady_=false;std::fill_n(multiplayerTextMemory_->begin()+0xead6,128,std::uint8_t{});}
}
NativeMenuRenderState NativeGameSession::multiplayerMenuState()const{
    NativeMenuRenderState state;state.selected=multiplayerPanel_==MultiplayerPanel::List?multiplayerListChoice_%3:multiplayerChoice_;state.division=(*data_.memory)[0x1c9ce];
    switch(multiplayerPanel_){
    case MultiplayerPanel::Select:state.count=unsigned(multiplayerActions_.size()-1);break;
    case MultiplayerPanel::Configuration:state.count=3;break;
    case MultiplayerPanel::Title:case MultiplayerPanel::Delete:case MultiplayerPanel::CreateError:state.count=1;break;
    case MultiplayerPanel::Working:case MultiplayerPanel::Notice:state.count=0;break;
    case MultiplayerPanel::List:{const auto first=(multiplayerListChoice_/3)*3;state.count=multiplayerList_.empty()?0:unsigned(std::min<std::size_t>(3,multiplayerList_.size()-first))-1;break;}
    default:state.count=2;break;
    }return state;
}
NativeMenuImage NativeGameSession::multiplayerImage()const{
    std::string heading="Multiplayer",detail;std::vector<std::string> rows;unsigned selected=multiplayerChoice_;
    switch(multiplayerPanel_){
    case MultiplayerPanel::Select:
        for(auto action:multiplayerActions_)rows.push_back(action==HomeAction::Continue?"Continue":action==HomeAction::NewCareer?"New Game":"Browse Saved Games");
        for(const auto& entry:multiplayerList_)if(entry.id==lastMultiplayer_)detail=entry.name;break;
    case MultiplayerPanel::Home:heading=activeMultiplayer_->name;rows={multiplayerSuspended_?"Resume Championship":"Start Championship","Practise","Saved Games"};detail=summary(*activeMultiplayer_);break;
    case MultiplayerPanel::Title:heading="Game name";rows={"Continue","Cancel"};detail=(multiplayerTitle_.size()>29?multiplayerTitle_.substr(multiplayerTitle_.size()-29):multiplayerTitle_)+"_\nType a name, then confirm";break;
    case MultiplayerPanel::Configuration:heading="Championship setup";rows={"Division: "+std::to_string(4-multiplayerDivision_),std::string("League: ")+(multiplayerLeague_?"Super":"Normal"),"Continue","Cancel"};break;
    case MultiplayerPanel::List:{heading="Browse Saved Games";const auto first=(multiplayerListChoice_/3)*3;
        for(unsigned n=first;n<multiplayerList_.size()&&n<first+3;++n)rows.push_back(multiplayerList_[n].name+(multiplayerList_[n].state==SavedGameFileState::Conflict?" [copy]":""));
        if(rows.empty()){rows={"Back"};detail="No saved games";selected=0;}else{selected=multiplayerListChoice_-first;const auto& entry=multiplayerList_[multiplayerListChoice_];detail=folderLabel(entry.folder)+"\n"+summary(entry);}break;}
    case MultiplayerPanel::Actions:heading=multiplayerList_[multiplayerListChoice_].name;rows={"Continue","Delete Game","Back"};detail=summary(multiplayerList_[multiplayerListChoice_]);break;
    case MultiplayerPanel::Delete:heading="Delete game?";rows={"Cancel","Delete permanently"};detail="Game and recovery copies";break;
    case MultiplayerPanel::Conflict:{const auto& incoming=multiplayerList_[multiplayerListChoice_];heading="Resolve game copy";rows={"Replace other copies","Keep Both","Cancel"};detail="Use: "+folderLabel(incoming.folder)+"\n"+summary(incoming);unsigned others=0;
        for(const auto& copy:multiplayerList_)if(copy.id==incoming.id&&copy.folder!=incoming.folder){if(!others)detail+="\nOther: "+folderLabel(copy.folder)+"\n"+summary(copy);++others;}if(others>1)detail+="\nAnd "+std::to_string(others-1)+" other copies";break;}
    case MultiplayerPanel::Working:heading=multiplayerWorkKind_==MultiplayerWork::Copy?"Copying game":multiplayerWorkKind_==MultiplayerWork::Delete?"Deleting game":"Cancelling setup";rows={"Working..."};detail="Please wait";break;
    case MultiplayerPanel::CreateError:heading="Game not created";rows={"Retry","Cancel"};detail="A first save is required";break;
    case MultiplayerPanel::Notice:heading="Multiplayer notice";rows={"OK"};detail=multiplayerNotice_;break;
    default:rows={"Back"};break;
    }return makeNativeChoiceImage(*data_.memory,heading,rows,selected,detail,saveStatus());
}
void NativeGameSession::beginMultiplayer(){
    multiplayerDraft_=true;multiplayerPractice_=false;multiplayerTitle_="New Game";multiplayerTextFresh_=true;multiplayerDivision_=multiplayerLeague_=0;
    multiplayerTextMemory_=std::make_unique<NativeMemory>(*checkpointBaseline_);newMultiplayer_.reset();newMultiplayerCheckpoint_.clear();showMultiplayerPanel(MultiplayerPanel::Title);
}
void NativeGameSession::beginMultiplayerRoster(){
    *data_.memory=*checkpointBaseline_;(*data_.memory)[0x1c9ce]=std::uint8_t(multiplayerDivision_);(*data_.memory)[0x1c9d0]=std::uint8_t(multiplayerLeague_);
    menu_.reset();opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);opening_->setNavigation(navigation_);multiplayerSetup_=true;creatingCareer_=false;finishPending_=false;previewPrepared_=false;transition_=Transition::None;nameLengths_.fill(0);
    multiplayerPanel_=MultiplayerPanel::None;NativeGameUpdate ignored;handleOpeningAction(opening_->beginMultiplayerName(),makeNativeMenuFrame(*data_.memory,multiplayerDivision_),ignored);releaseInput();
}
void NativeGameSession::registerMultiplayer(){
    newMultiplayer_=SavedGameEntry{NativeCareerStore::newId(),multiplayerTitle_,multiplayerDivision_,multiplayerLeague_,playedNow()};newMultiplayer_->players=(*data_.memory)[0x5eb76]+1;
    newMultiplayerCheckpoint_=captureMultiplayerCheckpoint();completeMultiplayerCreation();
}
void NativeGameSession::completeMultiplayerCreation(){
    careerStore_->retry();multiplayerStore_->save({newMultiplayer_->id,newMultiplayer_->name,newMultiplayer_->lastPlayed},newMultiplayerCheckpoint_);
    if(!flushStores()){showMultiplayerPanel(MultiplayerPanel::CreateError);return;}
    multiplayerStore_->setLastUsed(newMultiplayer_->id);if(!flushStores()){showMultiplayerPanel(MultiplayerPanel::CreateError);return;}
    activeMultiplayer_=multiplayerStore_->describe({newMultiplayer_->id,newMultiplayer_->name,newMultiplayer_->lastPlayed},newMultiplayerCheckpoint_);multiplayerCheckpoint_=std::move(newMultiplayerCheckpoint_);lastMultiplayer_=activeMultiplayer_->id;multiplayerSuspended_=false;multiplayerDraft_=false;newMultiplayer_.reset();
    activeCareer_.reset();checkpoint_.clear();suspendedSeason_.clear();multiplayerDirty_=false;showMultiplayerPanel(MultiplayerPanel::Home);
}
void NativeGameSession::cancelMultiplayerDraft(){
    if(newMultiplayer_){multiplayerWorkId_=newMultiplayer_->id;multiplayerWorkKind_=MultiplayerWork::CancelDraft;multiplayerWork_=multiplayerStore_->cancelDraft(newMultiplayer_->id,lastMultiplayer_);showMultiplayerPanel(MultiplayerPanel::Working);}
    else{multiplayerDraft_=false;showMultiplayerPanel(MultiplayerPanel::Select);}
}
void NativeGameSession::loadMultiplayer(const std::string& id){
    try{
        const auto saved=multiplayerStore_->read(id);if(saved.state==SavedGameFileState::Conflict)throw std::runtime_error("Resolve the copied game conflict first");
        MultiplayerBytes bytes;SavedGameEntry entry;
        if(activeMultiplayer_&&activeMultiplayer_->id==id&&!multiplayerCheckpoint_.empty()){bytes=multiplayerCheckpoint_;entry=*activeMultiplayer_;}
        else{if(!usable(saved.state))throw std::runtime_error(saved.state==SavedGameFileState::Unsupported?"Unsupported game version":"Saved game is unavailable");
            const auto game=decodeNativeMultiplayerSave(*checkpointBaseline_,saved.data);bytes=game.checkpoint;for(const auto& item:multiplayerStore_->list())if(item.id==id)entry=item;}
        const auto c=decodeNativeMultiplayerCheckpoint(*checkpointBaseline_,bytes);restoreMultiplayerCheckpoint(bytes);activeMultiplayer_=entry;multiplayerCheckpoint_=std::move(bytes);multiplayerSuspended_=c.menu.competitive;multiplayerDraft_=multiplayerPractice_=multiplayerDirty_=false;
        activeMultiplayer_->lastPlayed=playedNow();lastMultiplayer_=id;multiplayerStore_->save({id,activeMultiplayer_->name,activeMultiplayer_->lastPlayed},multiplayerCheckpoint_);multiplayerStore_->setLastUsed(id);showMultiplayerPanel(MultiplayerPanel::Home);
        if(saved.state==SavedGameFileState::Recovered){multiplayerNotice_="Game recovery copy restored";multiplayerNoticeReturn_=MultiplayerPanel::Home;showMultiplayerPanel(MultiplayerPanel::Notice);}
    }catch(const std::exception& error){multiplayerNotice_=error.what();multiplayerNoticeReturn_=MultiplayerPanel::Select;showMultiplayerPanel(MultiplayerPanel::Notice);}
}
void NativeGameSession::showMultiplayerHome(){
    if(multiplayerPractice_){multiplayerPractice_=false;restoreMultiplayerCheckpoint(multiplayerCheckpoint_,{},errorAcknowledged_);}
    else if(multiplayerPanel_==MultiplayerPanel::None)saveMultiplayerCheckpoint(raceEnded());
    showMultiplayerPanel(MultiplayerPanel::Home);
}
void NativeGameSession::resumeMultiplayer(bool practice){
    multiplayerPractice_=false;restoreMultiplayerCheckpoint(multiplayerCheckpoint_,{},errorAcknowledged_);multiplayerPanel_=MultiplayerPanel::None;
    if(practice){multiplayerPractice_=true;menu_=std::make_unique<NativeMenuFlow>(*data_.memory);menu_->setNavigation(navigation_);(void)menu_->startPracticeSelection();}
    else if(!multiplayerSuspended_){(void)menu_->startCompetition();applyNativeMultiplayerScreenState(*data_.memory,NativeMultiplayerScreen::Matchup);multiplayerDirty_=true;saveMultiplayerCheckpoint();}
    releaseInput();
}
void NativeGameSession::saveMultiplayerCheckpoint(bool){
    if(!activeMultiplayer_||multiplayerDraft_||multiplayerPractice_||multiplayerPanel_!=MultiplayerPanel::None||careerPanel_!=CareerPanel::None||openingActive()||transitioning()||!multiplayer())return;
    multiplayerCheckpoint_=captureMultiplayerCheckpoint();const auto folder=activeMultiplayer_->folder;
    activeMultiplayer_=multiplayerStore_->describe({activeMultiplayer_->id,activeMultiplayer_->name,playedNow()},multiplayerCheckpoint_);activeMultiplayer_->folder=folder;multiplayerSuspended_=activeMultiplayer_->competition;
    multiplayerStore_->save({activeMultiplayer_->id,activeMultiplayer_->name,activeMultiplayer_->lastPlayed},multiplayerCheckpoint_);multiplayerDirty_=false;
}
void NativeGameSession::observeMultiplayerState(NativeMenuPage previous){
    if(!multiplayerStore_||!activeMultiplayer_||multiplayerDraft_||multiplayerPanel_!=MultiplayerPanel::None||careerPanel_!=CareerPanel::None||openingActive()||!menu_)return;
    if(multiplayerPractice_){if(!transitioning()&&menu_->page()==NativeMenuPage::Main)showMultiplayerHome();return;}
    if(page()!=previous)multiplayerDirty_=true;
    if(!transitioning()&&multiplayerDirty_&&menu_->page()!=NativeMenuPage::Race)saveMultiplayerCheckpoint();
    if(!transitioning()&&menu_->page()==NativeMenuPage::Main)showMultiplayerHome();
}
bool NativeGameSession::updateMultiplayer(double elapsed,const NativeGameInput& held,NativeGameUpdate& result){
    if(!multiplayerStore_||careerPanel_!=CareerPanel::None)return false;
    const auto pulse=multiplayerClock_.sample(held.menu,elapsed);
    if(multiplayerPanel_==MultiplayerPanel::Working){
        if(multiplayerWork_.valid()&&multiplayerWork_.wait_for(std::chrono::seconds(0))==std::future_status::ready){const auto work=multiplayerWork_.get();const bool closing=closeAfterCareerCopy_;closeAfterCareerCopy_=false;
            if(activeMultiplayer_&&activeMultiplayer_->id==multiplayerWorkId_&&multiplayerWorkKind_!=MultiplayerWork::CancelDraft){activeMultiplayer_.reset();multiplayerCheckpoint_.clear();multiplayerSuspended_=false;}
            if(!work.error.empty()){multiplayerNotice_=work.error;multiplayerNoticeReturn_=multiplayerWorkKind_==MultiplayerWork::CancelDraft?MultiplayerPanel::CreateError:MultiplayerPanel::List;showMultiplayerPanel(MultiplayerPanel::Notice);}
            else if(multiplayerWorkKind_==MultiplayerWork::CancelDraft){newMultiplayer_.reset();newMultiplayerCheckpoint_.clear();multiplayerDraft_=false;showMultiplayerPanel(MultiplayerPanel::Select);}
            else{lastMultiplayer_=multiplayerStore_->lastUsed();showMultiplayerPanel(MultiplayerPanel::List);for(unsigned n=0;n<multiplayerList_.size();++n)if(multiplayerList_[n].id==work.id)multiplayerListChoice_=n;}
            if(closing&&work.error.empty())(void)depart(Departure::Close);releaseInput(held.menu);
        }return true;
    }
    if(multiplayerPanel_!=MultiplayerPanel::None){
        if(storesFailed()&&!errorAcknowledged_&&!multiplayerDraft_&&multiplayerPanel_!=MultiplayerPanel::Notice){returnPanel_=CareerPanel::None;showCareerPanel(CareerPanel::SaveError);return false;}
        auto selection=pulse;bool textConfirm=false;
        if(multiplayerPanel_==MultiplayerPanel::Title){
            for(const auto& key:held.rawKeys)setNativeRawKey(*multiplayerTextMemory_,key.code,key.pressed);
            if(!multiplayerTextReady_){if((held.menu.controls&16)||(*multiplayerTextMemory_)[0xead6+0x43]||(*multiplayerTextMemory_)[0xead6+0x44])return true;multiplayerTextReady_=true;}
            const auto scan=scanNativeNameInput(*multiplayerTextMemory_,0);if(scan.available){const auto c=std::uint8_t(scan.code);
                if(c==13)textConfirm=true;else if(c==8){if(multiplayerTextFresh_)multiplayerTitle_.clear();else if(!multiplayerTitle_.empty())multiplayerTitle_.pop_back();multiplayerTextFresh_=false;}
                else if(c>=32&&c<127){if(multiplayerTextFresh_)multiplayerTitle_.clear();multiplayerTextFresh_=false;if(multiplayerTitle_.size()<32)multiplayerTitle_+=char(c);}
                else if(scan.code==0x4c00)selection.controls|=1;else if(scan.code==0x4d00)selection.controls|=2;
            }
        }
        const auto state=multiplayerMenuState();const auto count=state.count+1;
        if(multiplayerPanel_==MultiplayerPanel::List){if((selection.controls&1)&&multiplayerListChoice_)--multiplayerListChoice_;if((selection.controls&2)&&multiplayerListChoice_+1<multiplayerList_.size())++multiplayerListChoice_;
            if((selection.controls&4)&&multiplayerListChoice_>=3)multiplayerListChoice_-=3;if((selection.controls&8)&&!multiplayerList_.empty())multiplayerListChoice_=std::min(multiplayerListChoice_+3,unsigned(multiplayerList_.size()-1));}
        else{if((selection.controls&1)&&multiplayerChoice_)--multiplayerChoice_;if((selection.controls&2)&&multiplayerChoice_+1<count)++multiplayerChoice_;for(unsigned n=0;n<count&&n<4;++n)if(selection.numberedKeys&(1u<<n))multiplayerChoice_=n;}
        if(selection.back){
            switch(multiplayerPanel_){
            case MultiplayerPanel::Select:(void)depart(Departure::Mode);break;
            case MultiplayerPanel::Home:(void)depart(Departure::MultiplayerSelect);break;
            case MultiplayerPanel::Title:cancelMultiplayerDraft();break;
            case MultiplayerPanel::Configuration:showMultiplayerPanel(MultiplayerPanel::Title);break;
            case MultiplayerPanel::List:showMultiplayerPanel(MultiplayerPanel::Select);break;
            case MultiplayerPanel::Actions:case MultiplayerPanel::Conflict:showMultiplayerPanel(MultiplayerPanel::List);break;
            case MultiplayerPanel::Delete:showMultiplayerPanel(MultiplayerPanel::Actions);break;
            case MultiplayerPanel::CreateError:cancelMultiplayerDraft();break;
            case MultiplayerPanel::Notice:showMultiplayerPanel(multiplayerNoticeReturn_);break;
            default:break;
            }releaseInput(held.menu);return true;
        }
        if(multiplayerPanel_==MultiplayerPanel::Configuration&&(selection.controls&12)&&multiplayerChoice_<2){if(multiplayerChoice_==0)multiplayerDivision_=(multiplayerDivision_+((selection.controls&4)?3:1))%4;else multiplayerLeague_=multiplayerLeague_?0:11;return true;}
        if(!(selection.controls&16)&&!textConfirm)return true;const auto choice=multiplayerChoice_;
        switch(multiplayerPanel_){
        case MultiplayerPanel::Select:switch(multiplayerActions_.at(choice)){case HomeAction::Continue:(void)depart(Departure::LoadMultiplayer,lastMultiplayer_);break;case HomeAction::NewCareer:(void)depart(Departure::NewMultiplayer);break;case HomeAction::Browse:showMultiplayerPanel(MultiplayerPanel::List);break;}break;
        case MultiplayerPanel::Home:if(choice<2)resumeMultiplayer(choice==1);else (void)depart(Departure::MultiplayerSelect);break;
        case MultiplayerPanel::Title:
            if(choice==1)cancelMultiplayerDraft();else{while(!multiplayerTitle_.empty()&&multiplayerTitle_.back()==' ')multiplayerTitle_.pop_back();while(!multiplayerTitle_.empty()&&multiplayerTitle_.front()==' ')multiplayerTitle_.erase(0,1);if(!multiplayerTitle_.empty())showMultiplayerPanel(MultiplayerPanel::Configuration);}break;
        case MultiplayerPanel::Configuration:if(choice==0)multiplayerDivision_=(multiplayerDivision_+1)%4;else if(choice==1)multiplayerLeague_=multiplayerLeague_?0:11;else if(choice==2)beginMultiplayerRoster();else cancelMultiplayerDraft();break;
        case MultiplayerPanel::List:if(multiplayerList_.empty())showMultiplayerPanel(MultiplayerPanel::Select);else showMultiplayerPanel(multiplayerList_[multiplayerListChoice_].state==SavedGameFileState::Conflict?MultiplayerPanel::Conflict:MultiplayerPanel::Actions);break;
        case MultiplayerPanel::Actions:if(choice==0)(void)depart(Departure::LoadMultiplayer,multiplayerList_[multiplayerListChoice_].id);else showMultiplayerPanel(choice==1?MultiplayerPanel::Delete:MultiplayerPanel::List);break;
        case MultiplayerPanel::Delete:if(choice==0)showMultiplayerPanel(MultiplayerPanel::Actions);else{const auto entry=multiplayerList_[multiplayerListChoice_];multiplayerWorkId_=entry.id;multiplayerWorkKind_=MultiplayerWork::Delete;multiplayerWork_=multiplayerStore_->erase(entry);showMultiplayerPanel(MultiplayerPanel::Working);}break;
        case MultiplayerPanel::Conflict:if(choice==2)showMultiplayerPanel(MultiplayerPanel::List);else try{const auto entry=multiplayerList_[multiplayerListChoice_];multiplayerWorkId_=entry.id;multiplayerWorkKind_=MultiplayerWork::Copy;multiplayerWork_=multiplayerStore_->resolveCopy(entry,choice==0?SavedGameCopyChoice::Replace:SavedGameCopyChoice::KeepBoth);showMultiplayerPanel(MultiplayerPanel::Working);}catch(const std::exception& error){multiplayerNotice_=error.what();multiplayerNoticeReturn_=MultiplayerPanel::List;showMultiplayerPanel(MultiplayerPanel::Notice);}break;
        case MultiplayerPanel::CreateError:if(choice==0)completeMultiplayerCreation();else cancelMultiplayerDraft();break;
        case MultiplayerPanel::Notice:showMultiplayerPanel(multiplayerNoticeReturn_);break;
        default:break;
        }
        if(multiplayerPanel_==MultiplayerPanel::Title)for(const auto& key:held.rawKeys)setNativeRawKey(*multiplayerTextMemory_,key.code,key.pressed);
        releaseInput(held.menu);return true;
    }
    if(multiplayerDraft_&&openingActive()&&pulse.back){
        if(opening_->page()==NativeOpeningPage::Name&&(*data_.memory)[0x5eb76]==0){showMultiplayerPanel(MultiplayerPanel::Configuration);return true;}
        const auto previous=menuImage();opening_->setNavigation(NativeMenuNavigation::Responsive);const auto action=opening_->back();opening_->setNavigation(navigation_);handleOpeningAction(action,previous,result);releaseInput(held.menu);return true;
    }
    if(activeMultiplayer_&&!multiplayerDraft_&&!transitioning()&&menu_){
        if(pulse.back&&(page()!=NativeMenuPage::Race||raceEnded())){
            if(multiplayerPractice_&&page()!=NativeMenuPage::Main&&page()!=NativeMenuPage::Race){menu_->setNavigation(NativeMenuNavigation::Responsive);(void)menu_->back();menu_->setNavigation(navigation_);if(menu_->page()!=NativeMenuPage::Main){releaseInput(held.menu);return true;}}
            showMultiplayerHome();releaseInput(held.menu);return true;
        }
        if(finishPending_&&!multiplayerPractice_){if(pulse.controls&16){finishPending_=false;saveMultiplayerCheckpoint();releaseInput(held.menu);result.raceExited=true;}return true;}
    }
    return false;
}
} // namespace scr::runtime

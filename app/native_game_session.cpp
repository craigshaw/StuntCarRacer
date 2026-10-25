#include "native_game_session.h"
#include "native_race_initialization.h"
#include "track_initialization.h"
#include "planar_image.h"
#include "race_transitions.h"
#include "native_competition_render.h"
#include "native_multiplayer_render.h"
#include "native_hall_of_fame.h"
#include "state/draw_bridge_adapter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
namespace {
NativeStartupData menuStartup(const std::filesystem::path& pack,NativeGameStart start){
    auto data=startNativeGame(pack);
    if(start==NativeGameStart::EstablishedMenu)initializeNativeMenuSession(*data.memory);
    else initializeNativeOpeningSession(*data.memory);
    return data;
}
void restoreCockpit(NativeMemory& memory){
    // Original copyMainGameBackground0x1bace, performed for every race, even
    // when the same menu session already owns prepared sprites and audio.
    const auto image=decodePlanarRle(memory.data()+0x201a4,memory.size()-0x201a4,320,200,4);
    for(unsigned offset:{0u,32000u})std::copy(image.planes.begin(),image.planes.end(),memory.begin()+0x6a594+offset);
}
NativePalette imagePalette(const NativeMenuImage& image){
    NativePalette palette{};
    // originalPaletteRgb expands each original0..7 channel into an OCS nibble.
    // Undo that expansion before feeding the original palette fade routine.
    for(unsigned i=0;i<palette.size();++i){const auto rgb=image.palette[i];palette[i]=std::uint16_t(((rgb>>21)&7)<<8|((rgb>>13)&7)<<4|((rgb>>5)&7));}
    return palette;
}
std::optional<NativeMultiplayerScreen> multiplayerScreen(NativeMenuPage page){
    switch(page){
    case NativeMenuPage::Matchup:return NativeMultiplayerScreen::Matchup;
    case NativeMenuPage::Records:return NativeMultiplayerScreen::Records;
    case NativeMenuPage::RoundResults:return NativeMultiplayerScreen::RoundResults;
    case NativeMenuPage::Standings:return NativeMultiplayerScreen::Standings;
    case NativeMenuPage::RaceResults:return NativeMultiplayerScreen::RaceResults;
    case NativeMenuPage::TrackResults:return NativeMultiplayerScreen::TrackResults;
    default:return std::nullopt;
    }
}
}
NativeMenuImage NativeGameSession::menuImage()const{
    if(careerPanel_!=CareerPanel::None)return careerImage();
    if(saveReturn_==SaveReturn::Roster)return makeNativeCompetitionImage(*data_.memory,NativeMenuPage::Roster,(*data_.memory)[0x1c9ce]);
    if(saveReturn_==SaveReturn::Standings)return makeNativeOpeningStandingsImage(*data_.memory);
    if(saveMenu_)return saveMenu_->image();
    if(page()==NativeMenuPage::HallOfFame)return makeNativeHallOfFameImage(*data_.memory);
    if(openingActive()){
        switch(opening_->page()){
        case NativeOpeningPage::Name:return nameScreen_->image();
        case NativeOpeningPage::Roster:return makeNativeCompetitionImage(*data_.memory,NativeMenuPage::Roster,(*data_.memory)[0x1c9ce]);
        case NativeOpeningPage::LocalStandings:return makeNativeOpeningStandingsImage(*data_.memory);
        default:return makeNativeMenuImage(*data_.memory,opening_->renderState());
        }
    }
    if(menu_->multiplayer())if(const auto screen=multiplayerScreen(page()))
        return makeNativeMultiplayerImage(*data_.memory,*screen,menu_->competitionDivision(),menu_->competitionContext());
    if(page()!=NativeMenuPage::LoadSave&&page()>=NativeMenuPage::Matchup)return makeNativeCompetitionImage(*data_.memory,page(),menu_->competitionDivision(),menu_->competitionContext());
    if(careerStore_&&activeCareer_&&!(*data_.memory)[0x5eb76]&&page()==NativeMenuPage::Main)
        return makeNativeChoiceImage(*data_.memory,"Division "+std::to_string(4-(*data_.memory)[0x1c9ce]),
            {"Hall of Fame","Practise",hasSuspendedSeason()?"Resume Season":"Start Season"},menu_->renderState().selected,{},{});
    return makeNativeMenuImage(*data_.memory,menu_->renderState());
}
NativeGameSession::NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings,NativeGameStart start,const std::filesystem::path& saveFile):
    data_(menuStartup(pack,start)),nameEntry_(*data_.memory),saveStore_(saveFile),recordings_(recordings){
    if(start==NativeGameStart::EstablishedMenu)menu_=std::make_unique<NativeMenuFlow>(*data_.memory);
    else opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);
    const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce],start==NativeGameStart::EstablishedMenu);
    transitionIndices_.assign(image.indices.begin(),image.indices.end());
    fade_.reset({});fade_.animateTo(readNativePalette(*data_.memory,0x23c32));
}
void NativeGameSession::preparePreview(){
    const unsigned track=menu_->trackId(),league=(*data_.memory)[0x1c9d0];
    if((careerStore_||navigation_==NativeMenuNavigation::Responsive)&&previewPrepared_&&preparedTrack_==track&&
            preparedLeague_==league&&preparedCompetitive_==menu_->competitive()){
        preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);return;
    }
    initializeNativePlayerConfiguration(*data_.memory);
    initializeNativeTrack(*data_.memory,menu_->trackId());
    (*data_.memory)[0x691b3]=(*data_.memory)[0x1c9d0]?8:9;
    (*data_.memory)[0x691b2]=(*data_.memory)[0x1c9d0]?4:10;
    if(state::drawBridge::animate(*data_.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*data_.memory);
    preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);
    previewPrepared_=true;preparedTrack_=track;preparedLeague_=league;preparedCompetitive_=menu_->competitive();
}
void NativeGameSession::startRace(){
    // Commit the prepared fixture while still in the menu handoff. Active
    // driving never waits for disk I/O; a failure is offered before tick one.
    if(careerStore_)(void)careerStore_->flush();
    previewPrepared_=false;
    restoreCockpit(*data_.memory);initializeNativeRace(*data_.memory);
    race_.loadNativeState(*data_.memory,data_.payload,recordings_);
    race_.setRunningLapHundredths(true);
    driveClock_.reset();driveLatch_.clear();endFireWait_.reset();quitPending_=false;
}
void NativeGameSession::leaveRace(){
    transitionIndices_=race_.captureLiveIndices(race_.frames().back().action==unsigned(RaceTransitionAction::raceEnd),presentationView_);
    fade_.fadeToColor(0);transition_=Transition::OutMenu;
    driveClock_.reset();releaseInput();
}
void NativeGameSession::advanceTransition(double elapsed,NativeGameUpdate& result){
    if(transition_==Transition::SaveDelay){
        raceDelay_-=elapsed;if(raceDelay_>0)return;
        NativeMenuImage previous;std::transform(transitionIndices_.begin(),transitionIndices_.end(),previous.indices.begin(),[](std::uint16_t index){return std::uint8_t(index);});
        previous.palette=fade_.rgbPalette();
        saveMenu_=std::make_unique<NativeSaveMenu>(*data_.memory,saveWriting_,&previous);
        saveMenu_->setNavigation(navigation_);
        transition_=Transition::None;return;
    }
    if(transition_==Transition::RaceDelay){
        raceDelay_-=elapsed;
        if(raceDelay_<=0){fade_.animateTo(readNativePalette(*data_.memory,0x20184));transition_=Transition::InRace;}
        return;
    }
    fade_.advance(elapsed);if(fade_.active())return;
    switch(transition_){
    case Transition::OutPreview:
        preparePreview();transitionIndices_.assign(preview_.indices().begin(),preview_.indices().end());
        fade_.animateTo(readNativePalette(*data_.memory,0x2b974));transition_=Transition::InPreview;break;
    case Transition::OutRace:
        startRace();transitionIndices_=race_.captureLiveIndices(false,presentationView_);
        raceDelay_=nativeMenuDelaySeconds(3);transition_=Transition::RaceDelay;break;
    case Transition::OutMenu:{
        race_.finishLiveExit(*data_.memory);
        menu_->returnFromRace();const auto image=menuImage();
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(imagePalette(image));
        transition_=Transition::InMenu;result.raceExited=true;break;
    }
    case Transition::OutCompetition:{
        const auto image=menuImage();transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(imagePalette(image));transition_=Transition::InCompetition;break;
    }
    case Transition::OutOpening:{
        // displayMenuScreen fades its empty division frame in before the
        // main options are painted. The roster is fully drawn before its fade.
        const auto image=openingActive()?menuImage():makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]);
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(imagePalette(image));transition_=Transition::InOpening;break;
    }
    case Transition::OutHall:{
        prepareNativeHallOfFame(*data_.memory);const auto image=menuImage();
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(readNativePalette(*data_.memory,0x30bd0));transition_=Transition::InHall;break;
    }
    case Transition::OutHallMenu:{
        (*data_.memory)[0x5d724]=0x41;
        const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]);
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(readNativePalette(*data_.memory,0x23c32));transition_=Transition::InHallMenu;break;
    }
    case Transition::OutSaveRoster:{
        const auto image=menuImage();transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(readNativePalette(*data_.memory,0x35a76));transition_=Transition::InSaveRoster;break;
    }
    case Transition::OutReplayFrame:
    case Transition::OutRestartOpening:{
        const bool restarting=transition_==Transition::OutRestartOpening;
        (*data_.memory)[0x5d724]=0x41;
        const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce],!restarting);
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.animateTo(readNativePalette(*data_.memory,0x23c32));
        transition_=restarting?Transition::InOpening:Transition::InReplayFrame;break;
    }
    case Transition::InReplayFrame:{
        const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]);
        transition_=Transition::None;showSaveReturn(image);break;
    }
    case Transition::InRace:transition_=Transition::None;result.raceStarted=true;break;
    default:transition_=Transition::None;break;
    }
}
void NativeGameSession::setNavigation(NativeMenuNavigation mode){
    if(navigation_!=mode){
        navigationClock_.clear();
        if(mode==NativeMenuNavigation::Responsive&&enteringName()){
            const unsigned length=nameEntry_.state().position;
            nameLengths_[(*data_.memory)[0x5eb76]]=length;
            (void)nameEntry_.beginResponsive(length);
        }
    }
    navigation_=mode;
    if(menu_)menu_->setNavigation(mode);
    if(opening_)opening_->setNavigation(mode);
    if(saveMenu_)saveMenu_->setNavigation(mode);
}
unsigned NativeGameSession::navigationScreen()const noexcept{
    if(openingActive())return 100+unsigned(opening_->page());
    if(saveMenu_)return 200+unsigned(saveMenu_->phase());
    if(saveReturn_!=SaveReturn::None)return 300+unsigned(saveReturn_);
    return unsigned(page());
}
void NativeGameSession::finishResponsiveTransitions(NativeGameUpdate& result){
    // Complete visual/delay phases through their existing one-shot handlers;
    // no simulation time is advanced by these presentation-only updates.
    for(unsigned n=0;transitioning()&&n<32;++n)advanceTransition(10,result);
    if(transitioning())throw std::logic_error("Navigation transition did not complete");
}
NativeGameUpdate NativeGameSession::update(double elapsed,const NativeGameInput& held){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid native game elapsed time");
    NativeGameUpdate managed;if(updateCareers(elapsed,held,managed))return managed;
    const auto oldPage=page();
    if(navigation_==NativeMenuNavigation::Original){const auto screen=navigationScreen();auto result=updateImpl(elapsed,held);observeCareerState(oldPage);if(screen!=navigationScreen())careerClock_.enterScreen(held.menu);return result;}
    NativeGameUpdate initial;finishResponsiveTransitions(initial);
    const auto previous=navigationScreen();
    NativeGameInput input=held;input.menu=navigationClock_.sample(held.menu,elapsed);
    input.rawKeys.erase(std::remove_if(input.rawKeys.begin(),input.rawKeys.end(),[](const auto& key){return key.code==0x45;}),input.rawKeys.end());
    setNativeRawKey(*data_.memory,0x45,false);
    auto result=updateImpl(elapsed,input);finishResponsiveTransitions(result);
    result.raceStarted=result.raceStarted||initial.raceStarted;result.raceExited=result.raceExited||initial.raceExited;
    observeCareerState(oldPage);
    if(previous!=navigationScreen()){
        navigationClock_.enterScreen(held.menu);
        careerClock_.enterScreen(held.menu);
        // A menu's initiating Return/arrow must not become text on arrival.
        for(unsigned key=0;key<128;++key)if(key!=0x60&&key!=0x61)(*data_.memory)[0xead6+key]=0;
    }
    return result;
}
NativeGameUpdate NativeGameSession::updateImpl(double elapsed,const NativeGameInput& input){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid native game elapsed time");
    NativeGameUpdate result;
    for(const auto& key:input.rawKeys)setNativeRawKey(*data_.memory,key.code,key.pressed);
    if(transitioning()){advanceTransition(elapsed,result);return result;}
    if(openingActive()){updateOpening(input,elapsed,result);return result;}
    if(saveMenu_){
        if(navigation_==NativeMenuNavigation::Responsive&&input.menu.back){
            handleSaveAction(saveMenu_->back());return result;
        }
        if(input.quit)setNativeRawKey(*data_.memory,0x45,true);
        handleSaveAction(saveMenu_->update(input.menu.controls,elapsed));return result;
    }
    if(saveReturn_!=SaveReturn::None){
        if(navigation_==NativeMenuNavigation::Responsive?bool((input.menu.controls&16)||input.menu.back):saveReturnFire_.update(input.menu.controls,elapsed)){
            const auto previous=menuImage();const bool roster=saveReturn_==SaveReturn::Roster;
            saveReturn_=SaveReturn::None;menu_->returnFromSave();
            if(roster){
                transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
                fade_.fadeToColor(readNativePalette(*data_.memory,0x23c32)[0]);transition_=Transition::OutHallMenu;
            }
        }
        return result;
    }
    if(page()==NativeMenuPage::Race){
        if(race_.liveTerminal()){
            // Original end-message waits for a new fire press. Escape follows
            // the separate immediate Practice exit path in the outer loop.
            if(navigation_==NativeMenuNavigation::Responsive?bool(input.menu.controls&16):endFireWait_.update(input.menu.controls,elapsed)){
                leaveRace();
            }
            return result;
        }
        driveLatch_.sample(input.driveControls);
        quitPending_=quitPending_||input.quit;
        const auto steps=driveClock_.advance(elapsed);
        for(unsigned n=0;n<steps;++n){
            race_.stepLive(driveLatch_.consume(input.drivePreset),quitPending_,input.drivePreset);quitPending_=input.quit;
            result.audio.push_back(race_.frames().back().audio);
            observeHall();
            if(race_.liveTerminal()){
                if(careerStore_){
                    saveCheckpoint(true);
                    // Driving has stopped. Commit the outcome before exposing
                    // its acknowledgement screen, or report the failed write.
                    (void)careerStore_->flush();
                }
                if(race_.frames().back().action==unsigned(RaceTransitionAction::quit)){
                    leaveRace();
                }else endFireWait_.reset();
                break;
            }
        }
        return result;
    }
    const auto previous=menuImage();const auto oldPage=page();
    const auto action=menu_->update(input.menu,elapsed);
    if(oldPage==NativeMenuPage::Main&&menu_->competitive())previewPrepared_=false;
    if(menu_->multiplayer()&&(action==NativeMenuAction::Changed||action==NativeMenuAction::ShowCompetitionPage))
        if(const auto screen=multiplayerScreen(page()))applyNativeMultiplayerScreenState(*data_.memory,*screen);
    if(action==NativeMenuAction::LoadGame||action==NativeMenuAction::SaveGame){
        saveLoaded_=false;pendingSave_.reset();
        const bool write=action==NativeMenuAction::SaveGame;
        beginNativeSaveTransaction(*data_.memory,write);
        saveWriting_=write;raceDelay_=nativeMenuDelaySeconds(20);
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.reset(readNativePalette(*data_.memory,0x23c32));transition_=Transition::SaveDelay;
    }else if(action==NativeMenuAction::ReplayGame){
        replayGame(previous);
    }else if(action==NativeMenuAction::ShowHallOfFame){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(readNativePalette(*data_.memory,0x30bd0)[0]);transition_=Transition::OutHall;
    }else if(action==NativeMenuAction::LeaveHallOfFame){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(readNativePalette(*data_.memory,0x23c32)[0]);transition_=Transition::OutHallMenu;
    }else if(action==NativeMenuAction::EnterPreview){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(readNativePalette(*data_.memory,0x2b974)[0]);transition_=Transition::OutPreview;
    }else if(action==NativeMenuAction::RotatePreview){
        preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);
    }else if(action==NativeMenuAction::StartPractice||action==NativeMenuAction::StartRace){
        transitionIndices_.assign(preview_.indices().begin(),preview_.indices().end());fade_.fadeToColor(0);transition_=Transition::OutRace;
    }else if(action==NativeMenuAction::ShowCompetitionPage){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(0);transition_=Transition::OutCompetition;
    }else if(action==NativeMenuAction::UnsupportedOption){
        result.unavailable=NativeGameUnavailable::menuRoute;
    }
    return result;
}
void NativeGameSession::releaseInput(const NativeMenuInput& held)noexcept{
    driveLatch_.clear();quitPending_=false;
    navigationClock_.clear();navigationClock_.enterScreen(held);
    careerClock_.clear();careerClock_.enterScreen(held);
    std::fill_n(data_.memory->begin()+0xead6,128,std::uint8_t{});
}
void NativeGameSession::handleSaveAction(const NativeSaveMenuResult& result){
    auto& memory=*data_.memory;
    if(result.action==NativeSaveMenuAction::Cancel){finishSave(true);return;}
    if(result.action==NativeSaveMenuAction::Changed&&saveMenu_->phase()==NativeSaveMenuPhase::SaveSelection)pendingSave_.reset();
    if(result.action==NativeSaveMenuAction::ReadDirectory){
        NativeSaveBlock directory{};
        if(const auto error=saveStore_.readDirectory(directory)){saveMenu_->operationFailed(error);return;}
        std::copy(directory.begin(),directory.end(),memory.begin()+0x7a21a);
        (void)saveMenu_->directoryLoaded();return;
    }
    if(result.action!=NativeSaveMenuAction::Select)return;
    const auto kind=static_cast<NativeSaveKind>(result.kind);
    if(careerStore_&&kind==NativeSaveKind::Records){
        finishSave(true);notice_="Hall saves automatically";returnPanel_=CareerPanel::None;showCareerPanel(CareerPanel::Notice);return;
    }
    auto next=std::make_unique<NativeMemory>(memory);
    commitNativeSaveDirectory(*next,result.slot,result.name,saveMenu_->writing());
    NativeSaveBlock directory{};std::copy_n(next->begin()+0x7a21a,directory.size(),directory.begin());
    if(saveMenu_->writing()){
        if(!pendingSave_)pendingSave_=makeNativeSavePayload(memory,kind);
        if(const auto error=saveStore_.writeSlot(result.slot,directory,*pendingSave_)){
            saveMenu_->operationFailed(error);return;
        }
    }else{
        NativeSavePayload payload{};
        if(const auto error=saveStore_.readSlot(result.slot,payload)){saveMenu_->operationFailed(error);return;}
        if(const auto error=saveStore_.writeDirectory(directory)){saveMenu_->operationFailed(error);return;}
        std::copy(directory.begin(),directory.end(),memory.begin()+0x7a21a);
        if(!applyNativeSavePayload(memory,kind,payload)){
            if(kind==NativeSaveKind::Career)restoreNativeSaveTransaction(memory);
            memory[0x1bb94]=0x81;
            saveMenu_->invalidSaveData();return;
        }
        saveLoaded_=true;
        previewPrepared_=false;
    }
    std::copy(directory.begin(),directory.end(),memory.begin()+0x7a21a);
    memory[0x62bb6]=(*next)[0x62bb6];memory[0x62bb7]=(*next)[0x62bb7];finishSave();
}
void NativeGameSession::finishSave(bool cancelled){
    const auto previous=saveMenu_->image();const auto kind=(*data_.memory)[0x1bb6b];
    if(!kind&&!saveLoaded_)restoreNativeSaveTransaction(*data_.memory);
    saveMenu_->complete();saveMenu_.reset();pendingSave_.reset();
    if(cancelled&&navigation_==NativeMenuNavigation::Responsive){menu_->returnFromSave(true);return;}
    if(kind==0x40){menu_->returnFromSave();return;}
    showSaveReturn(previous);
}
void NativeGameSession::showSaveReturn(const NativeMenuImage& previous){
    saveReturnFire_.reset();
    if((*data_.memory)[0x5eb76]){
        prepareNativeOpeningLocalStandings(*data_.memory);saveReturn_=SaveReturn::Standings;return;
    }
    saveReturn_=SaveReturn::Roster;
    transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
    fade_.fadeToColor(readNativePalette(*data_.memory,0x35a76)[0]);transition_=Transition::OutSaveRoster;
}
void NativeGameSession::replayGame(const NativeMenuImage& previous){
    previewPrepared_=false;
    auto& memory=*data_.memory;
    if(memory[0xead6+0x50]){
        initializeNativeGameTables(memory);
        std::copy_n(memory.begin()+0x7a91a,256,memory.begin()+0x1ecaa);initializeNativeGameData(memory);
        memory[0x57c3c]=0;memory[0x57c62]=0x40;if(!memory[0x60fbc])memory[0x60fbc]=0x80;
        opening_=std::make_unique<NativeOpeningFlow>(memory);opening_->setNavigation(navigation_);nameLengths_.fill(0);menu_.reset();releaseInput();
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(readNativePalette(memory,0x23c32)[0]);transition_=Transition::OutRestartOpening;return;
    }
    if(prepareNativeReplay(memory)){showSaveReturn(previous);return;}
    transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
    fade_.fadeToColor(readNativePalette(memory,0x23c32)[0]);transition_=Transition::OutReplayFrame;
}
void NativeGameSession::handleOpeningAction(NativeOpeningAction action,const NativeMenuImage& previous,NativeGameUpdate& result){
    if(action==NativeOpeningAction::BeginName){
        if(careerStore_&&opening_->multiplayer()&&!multiplayerSetup_){(void)depart(Departure::Multiplayer);return;}
        if(careerStore_&&!opening_->multiplayer()&&!creatingCareer_){
            showCareerPanel(CareerPanel::Home);
            return;
        }
        nameScreen_=std::make_unique<NativeOpeningNameScreen>(*data_.memory);
        if(navigation_==NativeMenuNavigation::Responsive){
            const auto length=nameLengths_[(*data_.memory)[0x5eb76]];
            (void)nameEntry_.beginResponsive(length);nameScreen_->beginAttempt();
            const unsigned offset=(11-(*data_.memory)[0x5eb76])*16;
            for(unsigned n=0;n<length;++n)nameScreen_->drawCharacter((*data_.memory)[0x1ecab+offset+n]);
        }else if(nameEntry_.begin().beganAttempt)nameScreen_->beginAttempt();
    }else if(action==NativeOpeningAction::ShowRoster){
        // Local multiplayer's table is a direct repaint. Single-player's
        // portrait screen fades first to its own grey palette entry zero.
        if(opening_->page()==NativeOpeningPage::Roster){
            transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
            fade_.fadeToColor(readNativePalette(*data_.memory,0x35a76)[0]);transition_=Transition::OutOpening;
        }
    }else if(action==NativeOpeningAction::Complete){
        const bool single=(*data_.memory)[0x5eb76]==0;
        menu_=std::make_unique<NativeMenuFlow>(*data_.memory);
        menu_->setNavigation(navigation_);
        if(single){
            transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
            fade_.fadeToColor(readNativePalette(*data_.memory,0x23c32)[0]);transition_=Transition::OutOpening;
        }
        if(careerStore_&&creatingCareer_&&single)registerCareer();
    }else if(action==NativeOpeningAction::UnsupportedComputerLink){
        result.unavailable=NativeGameUnavailable::computerLink;
        opening_->returnToMode();
    }
}
void NativeGameSession::updateOpening(const NativeGameInput& input,double elapsed,NativeGameUpdate& result){
    const auto previous=menuImage();
    if(navigation_==NativeMenuNavigation::Responsive&&input.menu.back){
        handleOpeningAction(opening_->back(),previous,result);return;
    }
    if(enteringName()){
        const auto update=nameEntry_.update(input.menu.controls);
        if(update.beganAttempt)nameScreen_->beginAttempt();
        if(update.retriedBlank&&navigation_==NativeMenuNavigation::Responsive)(void)nameEntry_.beginResponsive(0);
        for(const auto character:update.characters)nameScreen_->drawCharacter(character);
        if(navigation_==NativeMenuNavigation::Responsive){
            unsigned length=nameEntry_.state().position;
            const unsigned offset=(11-(*data_.memory)[0x5eb76])*16;
            while(length&&(*data_.memory)[0x1ecab+offset+length-1]==' ')--length;
            nameLengths_[(*data_.memory)[0x5eb76]]=length;
        }
        if(update.complete){
            nameScreen_->complete();handleOpeningAction(opening_->nameCompleted(),nameScreen_->image(),result);
        }
        return;
    }
    handleOpeningAction(opening_->update(input.menu,elapsed),previous,result);
}
const std::vector<std::uint32_t>& NativeGameSession::render(bool interpolate,bool paused,NativeView view){
    presentationView_=view;
    if(careerPanel_!=CareerPanel::None){packNativeMenuPixels(careerImage(),pixels_);return pixels_;}
    if(finishPending_){pixels_=finishPixels_;return pixels_;}
    if(transitioning()){
        const auto palette=fade_.rgbPalette();pixels_.resize(transitionIndices_.size());
        const unsigned width=unsigned(pixels_.size()/200);
        for(unsigned i=0;i<transitionIndices_.size();++i){const auto index=transitionIndices_[i];
            pixels_[i]=index<16?palette[index]:replayPresentationColor(palette,index,i%width,i/width,width);
        }
        return pixels_;
    }
    if(page()==NativeMenuPage::Preview)return preview_.pixels();
    if(page()==NativeMenuPage::Race){
        // Set the presentation view even when paused or at the terminal panel.
        if(race_.liveTerminal()||paused)race_.renderLive(driveClock_.fraction(),false,interpolate,view);
        if(race_.liveTerminal())return race_.renderLiveMessage(0x3c,4);
        if(paused)return race_.renderLiveMessage(0x4c,2);
        return race_.renderLive(driveClock_.fraction(),false,interpolate,view);
    }
    packNativeMenuPixels(menuImage(),pixels_);return pixels_;
}
} // namespace scr::viewer

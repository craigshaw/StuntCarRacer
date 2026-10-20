#include "native_game_session.h"
#include "native_race_initialization.h"
#include "track_initialization.h"
#include "planar_image.h"
#include "race_transitions.h"
#include "native_competition_render.h"
#include "../tests/draw_bridge_fixture.h"
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
}
NativeMenuImage NativeGameSession::menuImage()const{
    if(openingActive()){
        switch(opening_->page()){
        case NativeOpeningPage::Name:return nameScreen_->image();
        case NativeOpeningPage::Roster:return makeNativeCompetitionImage(*data_.memory,NativeMenuPage::Roster,(*data_.memory)[0x1c9ce]);
        case NativeOpeningPage::LocalStandings:return makeNativeOpeningStandingsImage(*data_.memory);
        default:return makeNativeMenuImage(*data_.memory,opening_->renderState());
        }
    }
    if(page()>=NativeMenuPage::Matchup)return makeNativeCompetitionImage(*data_.memory,page(),menu_->competitionDivision(),menu_->competitionContext());
    return makeNativeMenuImage(*data_.memory,menu_->renderState());
}
NativeGameSession::NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings,NativeGameStart start):
    data_(menuStartup(pack,start)),nameEntry_(*data_.memory),recordings_(recordings){
    if(start==NativeGameStart::EstablishedMenu)menu_=std::make_unique<NativeMenuFlow>(*data_.memory);
    else opening_=std::make_unique<NativeOpeningFlow>(*data_.memory);
    const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce],start==NativeGameStart::EstablishedMenu);
    transitionIndices_.assign(image.indices.begin(),image.indices.end());
    fade_.reset({});fade_.animateTo(readNativePalette(*data_.memory,0x23c32));
}
void NativeGameSession::preparePreview(){
    initializeNativePlayerConfiguration(*data_.memory);
    initializeNativeTrack(*data_.memory,menu_->trackId());
    (*data_.memory)[0x691b3]=(*data_.memory)[0x1c9d0]?8:9;
    (*data_.memory)[0x691b2]=(*data_.memory)[0x1c9d0]?4:10;
    if(test::drawBridge::animate(*data_.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*data_.memory);
    preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);
}
void NativeGameSession::startRace(){
    restoreCockpit(*data_.memory);initializeNativeRace(*data_.memory);
    race_.loadNativeState(*data_.memory,data_.payload,recordings_);
    driveClock_.reset();driveLatch_.clear();endFireWait_.reset();quitPending_=false;
}
void NativeGameSession::leaveRace(){
    transitionIndices_=race_.captureLiveIndices(race_.frames().back().action==unsigned(RaceTransitionAction::raceEnd));
    fade_.fadeToColor(0);transition_=Transition::OutMenu;
    driveClock_.reset();releaseInput();
}
void NativeGameSession::advanceTransition(double elapsed,NativeGameUpdate& result){
    if(transition_==Transition::RaceDelay){
        raceDelay_-=elapsed;
        if(raceDelay_<=0){fade_.animateTo(readNativePalette(*data_.memory,0x20184));transition_=Transition::InRace;}
        return;
    }
    fade_.advance(elapsed);if(fade_.active())return;
    switch(transition_){
    case Transition::OutPreview:
        preparePreview();transitionIndices_=preview_.indices();
        fade_.animateTo(readNativePalette(*data_.memory,0x2b974));transition_=Transition::InPreview;break;
    case Transition::OutRace:
        startRace();transitionIndices_=race_.captureLiveIndices();
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
    case Transition::InRace:transition_=Transition::None;result.raceStarted=true;break;
    default:transition_=Transition::None;break;
    }
}
NativeGameUpdate NativeGameSession::update(double elapsed,const NativeGameInput& input){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid native game elapsed time");
    NativeGameUpdate result;
    for(const auto& key:input.rawKeys)setNativeRawKey(*data_.memory,key.code,key.pressed);
    if(transitioning()){advanceTransition(elapsed,result);return result;}
    if(openingActive()){updateOpening(input,elapsed,result);return result;}
    if(page()==NativeMenuPage::Race){
        if(race_.liveTerminal()){
            // Original end-message waits for a new fire press. Escape follows
            // the separate immediate Practice exit path in the outer loop.
            if(endFireWait_.update(input.menu.controls,elapsed)){
                leaveRace();
            }
            return result;
        }
        driveLatch_.sample(input.driveControls);
        quitPending_=quitPending_||input.quit;
        const auto steps=driveClock_.advance(elapsed);
        for(unsigned n=0;n<steps;++n){
            race_.stepLive(driveLatch_.consume(),quitPending_);quitPending_=input.quit;
            result.audio.push_back(race_.frames().back().audio);
            if(race_.liveTerminal()){
                if(race_.frames().back().action==unsigned(RaceTransitionAction::quit)){
                    leaveRace();
                }else endFireWait_.reset();
                break;
            }
        }
        return result;
    }
    const auto previous=menuImage();
    const auto action=menu_->update(input.menu,elapsed);
    if(action==NativeMenuAction::EnterPreview){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(readNativePalette(*data_.memory,0x2b974)[0]);transition_=Transition::OutPreview;
    }else if(action==NativeMenuAction::RotatePreview){
        preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);
    }else if(action==NativeMenuAction::StartPractice||action==NativeMenuAction::StartRace){
        transitionIndices_=preview_.indices();fade_.fadeToColor(0);transition_=Transition::OutRace;
    }else if(action==NativeMenuAction::ShowCompetitionPage){
        transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
        fade_.fadeToColor(0);transition_=Transition::OutCompetition;
    }else if(action==NativeMenuAction::UnsupportedOption){
        result.unavailable=menu_->unsupportedOption()==2&&(*data_.memory)[0x5eb76]?
            NativeGameUnavailable::localMultiplayerSeason:NativeGameUnavailable::menuRoute;
    }
    return result;
}
void NativeGameSession::releaseInput()noexcept{
    driveLatch_.clear();quitPending_=false;
    std::fill_n(data_.memory->begin()+0xead6,128,std::uint8_t{});
}
void NativeGameSession::handleOpeningAction(NativeOpeningAction action,const NativeMenuImage& previous,NativeGameUpdate& result){
    if(action==NativeOpeningAction::BeginName){
        nameScreen_=std::make_unique<NativeOpeningNameScreen>(*data_.memory);
        if(nameEntry_.begin().beganAttempt)nameScreen_->beginAttempt();
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
        if(single){
            transitionIndices_.assign(previous.indices.begin(),previous.indices.end());
            fade_.fadeToColor(readNativePalette(*data_.memory,0x23c32)[0]);transition_=Transition::OutOpening;
        }
    }else if(action==NativeOpeningAction::UnsupportedComputerLink){
        result.unavailable=NativeGameUnavailable::computerLink;
        opening_->returnToMode();
    }
}
void NativeGameSession::updateOpening(const NativeGameInput& input,double elapsed,NativeGameUpdate& result){
    const auto previous=menuImage();
    if(enteringName()){
        const auto update=nameEntry_.update(input.menu.controls);
        if(update.beganAttempt)nameScreen_->beginAttempt();
        for(const auto character:update.characters)nameScreen_->drawCharacter(character);
        if(update.complete){
            nameScreen_->complete();handleOpeningAction(opening_->nameCompleted(),nameScreen_->image(),result);
        }
        return;
    }
    handleOpeningAction(opening_->update(input.menu,elapsed),previous,result);
}
const std::vector<std::uint32_t>& NativeGameSession::render(bool interpolate,bool paused){
    if(transitioning()){
        const auto palette=fade_.rgbPalette();pixels_.resize(transitionIndices_.size());
        for(unsigned i=0;i<transitionIndices_.size();++i)pixels_[i]=palette[transitionIndices_[i]];
        return pixels_;
    }
    if(page()==NativeMenuPage::Preview)return preview_.pixels();
    if(page()==NativeMenuPage::Race){
        if(race_.liveTerminal())return race_.renderLiveMessage(0x3c,4);
        if(paused)return race_.renderLiveMessage(0x4c,2);
        return race_.renderLive(driveClock_.fraction(),false,interpolate);
    }
    packNativeMenuPixels(menuImage(),pixels_);return pixels_;
}
} // namespace scr::viewer

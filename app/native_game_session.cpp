#include "native_game_session.h"
#include "native_race_initialization.h"
#include "track_initialization.h"
#include "planar_image.h"
#include "race_transitions.h"
#include "../tests/draw_bridge_fixture.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
namespace {
NativeStartupData menuStartup(const std::filesystem::path& pack){
    auto data=startNativeGame(pack);initializeNativeMenuSession(*data.memory);return data;
}
void restoreCockpit(NativeMemory& memory){
    // Original copyMainGameBackground0x1bace, performed for every race, even
    // when the same menu session already owns prepared sprites and audio.
    const auto image=decodePlanarRle(memory.data()+0x201a4,memory.size()-0x201a4,320,200,4);
    for(unsigned offset:{0u,32000u})std::copy(image.planes.begin(),image.planes.end(),memory.begin()+0x6a594+offset);
}
}
NativeGameSession::NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings):
    data_(menuStartup(pack)),menu_(*data_.memory),recordings_(recordings){
    const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]);
    transitionIndices_.assign(image.indices.begin(),image.indices.end());
    fade_.reset({});fade_.animateTo(readNativePalette(*data_.memory,0x23c32));
}
void NativeGameSession::preparePreview(){
    initializeNativePlayerConfiguration(*data_.memory);
    initializeNativeTrack(*data_.memory,menu_.trackId());
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
        const auto image=makeNativeMenuFrame(*data_.memory,(*data_.memory)[0x1c9ce]);
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        menu_.returnFromRace();fade_.animateTo(readNativePalette(*data_.memory,0x23c32));
        transition_=Transition::InMenu;result.raceExited=true;break;
    }
    case Transition::InRace:transition_=Transition::None;result.raceStarted=true;break;
    default:transition_=Transition::None;break;
    }
}
NativeGameUpdate NativeGameSession::update(double elapsed,const NativeGameInput& input){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid native game elapsed time");
    NativeGameUpdate result;
    if(transitioning()){advanceTransition(elapsed,result);return result;}
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
    const auto previous=menu_.renderState();
    const auto action=menu_.update(input.menu,elapsed);
    if(action==NativeMenuAction::EnterPreview){
        const auto image=makeNativeMenuImage(*data_.memory,previous);
        transitionIndices_.assign(image.indices.begin(),image.indices.end());
        fade_.fadeToColor(readNativePalette(*data_.memory,0x2b974)[0]);transition_=Transition::OutPreview;
    }else if(action==NativeMenuAction::RotatePreview){
        preview_.load(*data_.memory,(*data_.memory)[0x1bb57]);
    }else if(action==NativeMenuAction::StartPractice){
        transitionIndices_=preview_.indices();fade_.fadeToColor(0);transition_=Transition::OutRace;
    }else if(action==NativeMenuAction::UnsupportedOption)result.unavailable=NativeGameUnavailable::menuRoute;
    return result;
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
    renderNativeMenu(*data_.memory,menu_.renderState(),pixels_);return pixels_;
}
} // namespace scr::viewer

#pragma once
// Native race orchestration over the recovered state layout.
// Typed C++ algorithms evolve this memory without executing original code.
#include "physics_update_adapter.h"
#include "player_controls_adapter.h"
#include "track_progress_adapter.h"
#include "race_timing_adapter.h"
#include "driving_effects_adapter.h"
#include "race_transitions_adapter.h"
#include "race_restart_adapter.h"
#include "draw_bridge_adapter.h"
#include "opponent_driving_adapter.h"
#include "opponent_collision_adapter.h"
#include <set>
#include <functional>

namespace scr::state::replay {
using Ram=scr::state::memory::ChipRam;
enum class ReplayPresentationStage { fuel, timing, speedometer, distance, wheels, resultIcon, wait };
using ReplayPresentationObserver=std::function<void(ReplayPresentationStage,const Ram&,const RaceTimingEffects&)>;
inline ControllerHardware hardware(unsigned mask,std::uint8_t direction=0xff) {
    unsigned joy=((mask&4)?0x200u:0u)|((mask&8)?2u:0u);
    if(bool(((joy>>1)^joy)&0x100)!=bool(mask&1))joy^=0x100;
    if(bool(((joy>>1)^joy)&1)!=bool(mask&2))joy^=1;
    return {std::uint16_t(joy),std::uint8_t((mask&16)?0x7f:0xff),direction};
}
struct NativeReplay {
    Ram ram;
    PhysicsData physicsData;
    TrackProgressData progressData;
    OpponentDrivingData opponentData;
    ControllerHardware controller{};
    std::vector<AudioRegisterWrite> audioWrites;
    DrivingParticleDraws particleDraws;
    std::uint8_t particleFrameCounter{};
    RaceRestartData restartData;
    RaceTransitionAction outerAction{};
    unsigned restarts{},waitVblanks{};
    bool trackFound{true};
    ReplayPresentationObserver presentation;
    void present(ReplayPresentationStage stage,const RaceTimingEffects& effects={}) {if(presentation)presentation(stage,ram,effects);}
    explicit NativeReplay(const Ram& initial):ram(initial),physicsData(physics::readData(initial)),progressData(trackProgress::readData(initial)),opponentData(opponent::readData(initial)),restartData(restart::readData(initial)) {
        if(ram[0x57c3c])throw std::invalid_argument("Replay requires local single-player mode");
        if(!physicsData.track.segmentCount)throw std::invalid_argument("Replay requires loaded track data");
    }
    void begin(unsigned mask) {controller=hardware(mask,controller.ciaDataDirection);--ram[0x1bbac];std::fill_n(ram.begin()+0xead6,128,std::uint8_t{});audioWrites.clear();}
    void controlsStage() {
        auto s=controls::readState(ram.data());const auto e=processLocalPlayerInput(controls::readInputs(ram.data(),controller),controls::readKeys(ram.data()),s);
        controls::writeState(ram.data(),s);if(e.joystickRead)controller.ciaDataDirection=e.ciaDataDirection;
        if(e.fuelHudRedraw)present(ReplayPresentationStage::fuel);
    }
    void physicsStage() {
        auto s=physics::readState(ram);physicsData.audio=audio::readData(ram.data());
        auto e=updateGamePhysics(physics::readInputs(ram),physicsData,s);physics::writeState(ram,s);audioWrites.insert(audioWrites.end(),e.begin(),e.end());
    }
    void engineStage() {auto s=effects::readState(ram);updateEnginePitch(effects::readInputs(ram),s);effects::writeState(ram,s);}
    void progressStage() {
        // prepareFrameRendering0x64e3c clears this per-frame geometry flag.
        ram[0x1bbba]=0;
        auto s=trackProgress::readState(ram);const bool found=updateTrackProgress(progressData,trackProgress::readInputs(ram),s);trackFound=found;trackProgress::writeState(ram,s);
        if(!(ram[0x1ca22]&0x80))return;
        opponent::logic(ram,opponentData);
        if(found){
            const auto distance=calculatePlayerDistance(contact::readDistanceData(ram.data()),contact::readDistanceInputs(ram.data()));
            contact::writeDistanceState(ram.data(),distance);
            auto contactState=contact::readState(ram.data());
            handleOpponentPositioning(contact::readData(ram.data()),contact::readInputs(ram.data()),contactState);
            contact::writeState(ram.data(),contactState);
        }
        opponent::geometry(ram,opponentData);
    }
    void sounds(const std::vector<std::uint8_t>& ids) {
        auto s=audio::readState(ram.data());const auto d=audio::readData(ram.data());
        for(auto id:ids){auto e=requestSample(id,d,s);audioWrites.insert(audioWrites.end(),e.begin(),e.end());}audio::writeState(ram.data(),s);
    }
    void groundStage() {particleDraws.clear();particleFrameCounter=ram[0x1bbac];auto s=effects::readState(ram);auto ids=updateGroundAndEdgeEffects(effects::readInputs(ram),s,&particleDraws);effects::writeState(ram,s);sounds(ids);}
    void wheelsStage() {auto s=effects::readState(ram);updateWheelVisuals(effects::readInputs(ram),physicsData.trig,s);effects::writeState(ram,s);present(ReplayPresentationStage::wheels);}
    void bridgeStage() {
        if(ram[0x1ca33]!=5)return;
        (void)drawBridge::animate(ram);
        // Both encoded heights and the special bridge guidance values are
        // mutable. All gameplay consumers must observe this tick's update.
        physicsData.track=scr::state::memory::decodeTrackGeometry(ram);
        progressData.track=physicsData.track;restartData.track=physicsData.track;opponentData.track.track=physicsData.track;
        std::copy_n(ram.begin()+0x1c952,100,opponentData.visibility.begin());
    }
    void damageStage() {auto s=effects::readState(ram);DrivingVisualEvents v;auto ids=updateDamageFeedback(effects::readInputs(ram),s,v);effects::writeState(ram,s);sounds(ids);}
    void timingStage() {auto s=timing::readState(ram.data());auto e=updateRaceTiming(timing::readInputs(ram.data()),s);timing::writeState(ram.data(),s);present(ReplayPresentationStage::timing,e);}
    void speedometerStage() {present(ReplayPresentationStage::speedometer);}
    void distanceStage() {present(ReplayPresentationStage::distance);}
    void disableAudio() {audioWrites.push_back({0xdff096,0xf});audioWrites.push_back({0xdff09a,0x780});}
    void swapStage() {
        present(ReplayPresentationStage::wait);
        auto s=transitions::readState(ram.data());waitVblanks+=s.framesToWait;s.framesToWait=0;
        if(!swapRaceBuffers(s))throw std::logic_error("Frame boundary failed");transitions::writeState(ram.data(),s);
    }
    void frameStage() {progressStage();groundStage();wheelsStage();}
    void restartStage(std::uint8_t segment) {
        ++restarts;
        restart::initialize(ram,segment,restartData,progressData,physicsData,audioWrites);
        restart::wheels(ram,physicsData.trig);restart::debris(ram);
        frameStage();timingStage();--ram[0x1bbac];swapStage();
        physicsStage();frameStage();timingStage();--ram[0x1bbac];speedometerStage();swapStage();
        ram[0x1bb72]=ram[0x5d724]=0x80;
        // Palette fades and platform delays have no admitted gameplay writes.
        // The comparator executes their CPU instructions at the wait boundary.
        waitVblanks+=ram[0x616d8];ram[0x616d8]=0;ram[0x616da]=0;
        ram[0x1bb1a]=ram[0x1bb1b]=0;
        scr::state::memory::writeWord(ram,0x69ede,0x8020);sounds({7});engineStage();
    }
    void outerStage() {
        auto s=transitions::readState(ram.data());
        auto e=inspectRaceEnd(transitions::readInputs(ram.data()),s);transitions::writeState(ram.data(),s);
        if(e.action==RaceTransitionAction::raceEnd&&!ram[0x1bbaf])present(ReplayPresentationStage::resultIcon);
        if(e.action==RaceTransitionAction::raceEnd) {
            swapStage();s=transitions::readState(ram.data());e=finishRaceEnd(s);transitions::writeState(ram.data(),s);disableAudio();
        } else {
            swapStage();s=transitions::readState(ram.data());
            auto pause=beginRacePause(transitions::readInputs(ram.data()),s);
            if(pause.action==RaceTransitionAction::pauseWait)throw std::invalid_argument("Paused capture needs an unpause timeline");
            e=continueRaceTransition(transitions::readInputs(ram.data()),s);transitions::writeState(ram.data(),s);
            if(e.action==RaceTransitionAction::restartRace){disableAudio();restartStage(e.restartSegment);}
        }
        outerAction=e.action;
    }
};
} // namespace scr::state::replay

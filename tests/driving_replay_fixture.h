#pragma once
// Verification adapter only. The original address space is a serialization
// format for independently evolving native state; no CPU executes this mirror.
// All production algorithms are the typed C++ functions in src/.
#include "physics_update_fixture.h"
#include "player_controls_fixture.h"
#include "track_progress_fixture.h"
#include "race_timing_fixture.h"
#include "driving_effects_fixture.h"
#include "race_transitions_fixture.h"
#include "race_restart_fixture.h"
#include "draw_bridge_fixture.h"
#include <set>

namespace scr::test::replay {
using Ram=scr_test::ChipRam;
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
    ControllerHardware controller{};
    std::vector<AudioRegisterWrite> audioWrites;
    RaceRestartData restartData;
    RaceTransitionAction outerAction{};
    unsigned restarts{},waitVblanks{};
    explicit NativeReplay(const Ram& initial):ram(initial),physicsData(physics::readData(initial)),progressData(trackProgress::readData(initial)),restartData(restart::readData(initial)) {
        if(ram[0x57c3c]||ram[0x1ca22]&0x80)throw std::invalid_argument("Replay currently admits local Practice captures");
        if(!physicsData.track.segmentCount)throw std::invalid_argument("Replay requires loaded track data");
    }
    void begin(unsigned mask) {controller=hardware(mask,controller.ciaDataDirection);--ram[0x1bbac];std::fill_n(ram.begin()+0xead6,128,std::uint8_t{});audioWrites.clear();}
    void controlsStage() {
        auto s=controls::readState(ram.data());const auto e=processLocalPlayerInput(controls::readInputs(ram.data(),controller),controls::readKeys(ram.data()),s);
        controls::writeState(ram.data(),s);if(e.joystickRead)controller.ciaDataDirection=e.ciaDataDirection;
    }
    void physicsStage() {
        auto s=physics::readState(ram);physicsData.audio=audio::readData(ram.data());
        auto e=updateGamePhysics(physics::readInputs(ram),physicsData,s);physics::writeState(ram,s);audioWrites.insert(audioWrites.end(),e.begin(),e.end());
    }
    void engineStage() {auto s=effects::readState(ram);updateEnginePitch(effects::readInputs(ram),s);effects::writeState(ram,s);}
    void progressStage() {auto s=trackProgress::readState(ram);(void)updateTrackProgress(progressData,trackProgress::readInputs(ram),s);trackProgress::writeState(ram,s);}
    void sounds(const std::vector<std::uint8_t>& ids) {
        auto s=audio::readState(ram.data());const auto d=audio::readData(ram.data());
        for(auto id:ids){auto e=requestSample(id,d,s);audioWrites.insert(audioWrites.end(),e.begin(),e.end());}audio::writeState(ram.data(),s);
    }
    void groundStage() {auto s=effects::readState(ram);auto ids=updateGroundAndEdgeEffects(effects::readInputs(ram),s);effects::writeState(ram,s);sounds(ids);}
    void wheelsStage() {auto s=effects::readState(ram);updateWheelVisuals(effects::readInputs(ram),physicsData.trig,s);effects::writeState(ram,s);}
    void bridgeStage() {
        if(ram[0x1ca33]!=5)return;
        (void)drawBridge::animate(ram);
        // Both encoded heights and the special bridge guidance values are
        // mutable. All gameplay consumers must observe this tick's update.
        physicsData.track=scr_test::decodeTrackGeometry(ram);
        progressData.track=physicsData.track;restartData.track=physicsData.track;
    }
    void damageStage() {auto s=effects::readState(ram);DrivingVisualEvents v;auto ids=updateDamageFeedback(effects::readInputs(ram),s,v);effects::writeState(ram,s);sounds(ids);}
    void timingStage() {auto s=timing::readState(ram.data());updateRaceTiming(timing::readInputs(ram.data()),s);timing::writeState(ram.data(),s);}
    void disableAudio() {audioWrites.push_back({0xdff096,0xf});audioWrites.push_back({0xdff09a,0x780});}
    void swapStage() {
        auto s=transitions::readState(ram.data());waitVblanks+=s.framesToWait;s.framesToWait=0;
        if(!swapRaceBuffers(s))throw std::logic_error("Frame boundary failed");transitions::writeState(ram.data(),s);
    }
    void frameStage() {progressStage();groundStage();wheelsStage();}
    void restartStage(std::uint8_t segment) {
        ++restarts;
        restart::initialize(ram,segment,restartData,progressData,physicsData,audioWrites);
        restart::wheels(ram,physicsData.trig);restart::debris(ram);
        frameStage();timingStage();--ram[0x1bbac];swapStage();
        physicsStage();frameStage();timingStage();--ram[0x1bbac];swapStage();
        ram[0x1bb72]=ram[0x5d724]=0x80;
        // Palette fades and platform delays have no admitted gameplay writes.
        // The comparator executes their CPU instructions at the wait boundary.
        waitVblanks+=ram[0x616d8];ram[0x616d8]=0;ram[0x616da]=0;
        ram[0x1bb1a]=ram[0x1bb1b]=0;
        scr_test::writeWord(ram,0x69ede,0x8020);sounds({7});engineStage();
    }
    void outerStage() {
        auto s=transitions::readState(ram.data());
        auto e=inspectRaceEnd(transitions::readInputs(ram.data()),s);transitions::writeState(ram.data(),s);
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
inline std::vector<unsigned> transitionOutputs() {
    std::set<unsigned> v;auto mark=[&](unsigned a,unsigned n){while(n--)v.insert(a++);};
    for(unsigned a:{0x1bb6f,0x1bbc8,0x1bb41,0x1bb6c,0x1bb75,0x1bbb4,0x1bb63,0x57c5b,0x1bba4,0x1bbb9,0x1bb8e,0x5e65a,0x5e65b,0x1bb8c,0x5dabe,0x616d8,0x1bcc8,0x1b816})mark(a,1);
    mark(0x1bbf4,2);mark(0x1bc62,2);mark(0x6a584,16);mark(0x1b812,4);
    return {v.begin(),v.end()};
}
inline std::vector<unsigned> physicsOutputs() {
    std::set<unsigned> values;auto mark=[&](unsigned a,unsigned n){for(unsigned i=0;i<n;++i)values.insert(a+i);};
    mark(0x1c230,72);mark(0x1bd02,12);mark(0x1bc94,12);mark(0x1bbf6,2);
    for(auto a:motion::outputWords)mark(a,2);
    for(auto a:wheelMotion::speedOutputs)mark(a,2);for(auto a:wheelMotion::dampingOutputs)mark(a,2);
    for(auto a:scr_test::trackSamplingOutputBytes())mark(a,1);
    for(auto r:suspension::observedRanges)mark(r.begin,r.end-r.begin);
    for(auto r:integration::stateRanges)mark(r.begin,r.end-r.begin);
    for(auto a:effects::stateWords)mark(a,2);for(auto a:effects::stateBytes)mark(a,1);mark(effects::seedAddress,4);
    for(auto a:steering::outputBytes())mark(a,1);
    for(auto a:carDynamics::wordOutputs)mark(a,2);mark(carDynamics::byteOutput,1);
    for(unsigned i=0;i<4;++i)mark(audio::channels+4*i,2);mark(audio::guard,2);
    // The suspension adapter's enclosing observed range also contains two
    // retained renderer bytes. Physics neither reads nor writes them. Original
    // rendering updates them between ticks, while this gameplay mirror keeps
    // its own presentation state. 1bb7c: writers66ff0/6704a/670c0, readers670c6/
    // 670f0. 1bb7f: writers64e2e/65036, readers6587a/65960. Captured races often
    // already held their eventual values; fresh startup exposes the difference.
    values.erase(0x1bb7c);values.erase(0x1bb7f);
    return {values.begin(),values.end()};
}
inline std::vector<unsigned> timingOutputs() {
    std::set<unsigned> v(timing::byteOutputs.begin(),timing::byteOutputs.end());
    for(auto r:timing::arrayOutputs)for(unsigned a=r.begin;a<r.end;++a)v.insert(a);
    // Renderer-owned scratch is retained on timing paths that do not use it.
    // Callers initialize the multiplier before use; its helper overwrites the
    // product. Icon and visibility bytes are HUD output only.
    for(unsigned a:{0x1bb1a,0x1bb1b,0x1bbe4,0x1bbe8,0x1bb8c,0x1bbde,0x1bb89})v.erase(a);
    return {v.begin(),v.end()};
}
} // namespace scr::test::replay

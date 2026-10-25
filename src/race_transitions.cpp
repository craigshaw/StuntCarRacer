#include "race_transitions.h"
#include <stdexcept>

namespace scr {
namespace {
void event(RaceTransitionEffects& e,RaceTransitionEventKind kind,std::uint8_t value=0) noexcept {
    e.events[e.count++]={kind,value};
}
bool negative(std::uint8_t n) noexcept {return (n&0x80)!=0;}
}
RaceTransitionEffects inspectRaceEnd(const RaceTransitionInputs& in,RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};
    if(!negative(s.frameBufferSyncMask&s.frameBufferToggle)||in.lapDisplayDuration||
        (!in.raceStartTimer&&!in.wheelMovementActive))return e;
    e.action=RaceTransitionAction::raceEnd;
    if(!in.raceComplete){s.resultsIcon=11;event(e,RaceTransitionEventKind::renderPlayerIcon,11);s.outcomeFlags=0xc0;}
    event(e,RaceTransitionEventKind::renderMessagePanel);
    s.messageActive=0x80;s.messageMode=4;s.messageIndex=0x3c;
    event(e,RaceTransitionEventKind::displayEndMessage,0x3c);
    s.enginePitchDelta=0xfff8;
    // The instruction is MOVE.B to the high byte of the wheel-speed word.
    s.wheelSpeed&=0x00ff;
    if(in.majorImpactCooldown==0x45&&in.holePosition)event(e,RaceTransitionEventKind::renderExistingHole,in.holePosition);
    return e;
}
bool swapRaceBuffers(RaceTransitionState& s) noexcept {
    if(s.framesToWait)return false;
    s.bufferSelector^=1;s.framesToWait=6;s.frameBufferToggle^=0x80;
    s.displayFrameBuffer=s.renderFrameBuffer=s.frameBuffers;
    if(negative(s.frameBufferToggle)){s.bufferToggleMirror=0x80;s.displayFrameBuffer+=0x7d00;}
    else{s.bufferToggleMirror=0;s.renderFrameBuffer+=0x7d00;}
    s.renderFrameBufferInset=s.renderFrameBuffer+0x284;
    s.pendingDisplayFrameBuffer=s.displayFrameBuffer;s.copperUpdatePending=0x80;
    return true;
}
RaceTransitionEffects finishRaceEnd(RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};e.action=RaceTransitionAction::raceEnd;
    s.gameExitFlag=s.networkInputSyncEnabled=0x80;
    event(e,RaceTransitionEventKind::updateNetworkFlags);
    event(e,RaceTransitionEventKind::disableAudio);
    event(e,RaceTransitionEventKind::waitForFire);return e;
}
RaceTransitionEffects finishLocalRaceExit(const RaceTransitionInputs& in,RaceTransitionState& s) {
    RaceTransitionEffects e{};e.action=RaceTransitionAction::raceEnd;
    s.dmaControl=0x20;e.fadeColor=in.menuFadeColor;event(e,RaceTransitionEventKind::fadeToMenu);
    s.displayUpdateFlag=0;event(e,RaceTransitionEventKind::disableAudio);
    if(!(in.selectedTrackInDivision&0x80))s.holePosition=in.savedHolePosition;
    // Original saves four seed bytes, then copies randomSeed3 onto the second
    // saved byte at 0x636b9. The enhanced source moved it to 0x636bc.
    s.savedRandomSeed=(in.randomSeed&0xff00ffffu)|(std::uint32_t(in.randomTail)<<16);
    s.networkInputSyncEnabled=0x80;return e;
}
RaceTransitionEffects continueRaceTransition(const RaceTransitionInputs& in,RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};
    if(!in.raceStartTimer&&negative(in.offTrackFlags)&&in.wheelMovementActive){
        if(negative(in.carHeightLow)||in.carHeightLow<2)s.raceCompletionCheckFlag=in.offTrackFlags;
        --s.restartCountdown;
        if(negative(s.restartCountdown)){
            ++s.restartCountdown;
            if(!s.blinkCountdown){
                event(e,RaceTransitionEventKind::disableAudio);e.action=RaceTransitionAction::restartRace;
                e.restartSegment=in.lastValidSegment;return e;
            }
        }
    }
    if(!in.raceStartTimer&&(negative(in.offTrackFlags)||!in.wheelMovementActive))return e;
    if(!in.quitPressed)return e;
    return quitLocalRace(in,s);
}
RaceTransitionEffects quitLocalRace(const RaceTransitionInputs& in,RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};
    if(!in.raceComplete)s.outcomeFlags=0xc0;
    event(e,RaceTransitionEventKind::updateNetworkFlags);e.action=RaceTransitionAction::quit;return e;
}
RaceTransitionEffects beginRacePause(const RaceTransitionInputs& in,RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};
    if(!negative(s.pauseState)){
        if(in.pausePressed)s.pauseState=0x80;
        return e;
    }
    s.pauseState=0;s.networkInputSyncEnabled=s.disableWheelUpdateFlag=0x80;
    event(e,RaceTransitionEventKind::disableAudio);s.enginePitchDelta=0;
    s.savedMessageActive=s.messageActive;s.savedMessageMode=s.messageMode;s.savedMessageIndex=s.messageIndex;
    event(e,RaceTransitionEventKind::renderMessagePanel);
    s.messageActive=0x80;s.messageMode=2;s.messageIndex=0x4c;
    event(e,RaceTransitionEventKind::waitForUnpause);e.action=RaceTransitionAction::pauseWait;return e;
}
RaceTransitionEffects resumeRacePause(RaceTransitionState& s) noexcept {
    RaceTransitionEffects e{};s.messageActive=s.savedMessageActive;s.messageMode=s.savedMessageMode;s.messageIndex=s.savedMessageIndex;
    s.networkInputSyncEnabled=s.disableWheelUpdateFlag=0;
    event(e,RaceTransitionEventKind::resumeEngine,7);return e;
}
} // namespace scr

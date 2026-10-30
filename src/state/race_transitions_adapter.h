#pragma once
#include "race/race_transitions.h"
#include "race_timing_adapter.h"
#include <array>
namespace scr::state::transitions {
using timing::word;using timing::longword;using timing::putWord;using timing::putLong;
#define SCR_TRANSITION_BYTES(X) \
 X(frameBufferSyncMask,0x1bb6f) X(frameBufferToggle,0x1bbc8) X(restartCountdown,0x1bb41) X(blinkCountdown,0x1bb6c) \
 X(raceCompletionCheckFlag,0x1bb75) X(outcomeFlags,0x1bbb4) X(gameExitFlag,0x1bb63) X(networkInputSyncEnabled,0x57c5b) \
 X(pauseState,0x1bba4) X(disableWheelUpdateFlag,0x1bbb9) X(messageActive,0x1bb8e) X(messageMode,0x5e65a) X(messageIndex,0x5e65b) \
 X(resultsIcon,0x1bb8c) X(bufferSelector,0x5dabe) X(framesToWait,0x616d8) X(bufferToggleMirror,0x1bcc8) X(copperUpdatePending,0x1b816) \
 X(displayUpdateFlag,0x5d724) X(holePosition,0x1c9cf)
#define SCR_TRANSITION_WORDS(X) X(enginePitchDelta,0x1bbf4) X(wheelSpeed,0x1bc62) X(dmaControl,0x69ede)
#define SCR_TRANSITION_LONGS(X) \
 X(frameBuffers,0x6a584) X(displayFrameBuffer,0x6a588) X(renderFrameBuffer,0x6a58c) X(renderFrameBufferInset,0x6a590) X(pendingDisplayFrameBuffer,0x1b812) X(savedRandomSeed,0x636b8)
inline RaceTransitionInputs readInputs(const std::uint8_t* r){
    return {r[0x1bbdf],r[0x1bb7e],r[0x1bb9c],r[0x1bcd0],r[0x1bb9b],r[0x1bbaf],r[0x1bbcc],r[0x1bb73],r[0x1c9cf],r[0xead6+0x19]==0xb3,r[0xead6+0x45]==0xb3,
        r[0x1ca22],r[0x1bbb5],r[0x625a6],word(r,0x23c32),longword(r,0x625a2)};
}
inline void writeInputs(std::uint8_t* r,const RaceTransitionInputs& in){
    r[0x1bbdf]=in.raceStartTimer;r[0x1bb7e]=in.wheelMovementActive;r[0x1bb9c]=in.offTrackFlags;r[0x1bcd0]=in.carHeightLow;
    r[0x1bb9b]=in.lastValidSegment;r[0x1bbaf]=in.raceComplete;r[0x1bbcc]=in.lapDisplayDuration;r[0x1bb73]=in.majorImpactCooldown;r[0x1c9cf]=in.holePosition;
    r[0xead6+0x19]=in.pausePressed?0xb3:0;r[0xead6+0x45]=in.quitPressed?0xb3:0;
    r[0x1ca22]=in.selectedTrackInDivision;r[0x1bbb5]=in.savedHolePosition;r[0x625a6]=in.randomTail;
    putWord(r,0x23c32,in.menuFadeColor);putLong(r,0x625a2,in.randomSeed);
}
inline RaceTransitionState readState(const std::uint8_t* r){RaceTransitionState s{};
#define READ_BYTE(field,address) s.field=r[address];
    SCR_TRANSITION_BYTES(READ_BYTE)
#undef READ_BYTE
#define READ_WORD(field,address) s.field=word(r,address);
    SCR_TRANSITION_WORDS(READ_WORD)
#undef READ_WORD
#define READ_LONG(field,address) s.field=longword(r,address);
    SCR_TRANSITION_LONGS(READ_LONG)
#undef READ_LONG
    return s;
}
inline void writeState(std::uint8_t* r,const RaceTransitionState& s){
#define WRITE_BYTE(field,address) r[address]=s.field;
    SCR_TRANSITION_BYTES(WRITE_BYTE)
#undef WRITE_BYTE
#define WRITE_WORD(field,address) putWord(r,address,s.field);
    SCR_TRANSITION_WORDS(WRITE_WORD)
#undef WRITE_WORD
#define WRITE_LONG(field,address) putLong(r,address,s.field);
    SCR_TRANSITION_LONGS(WRITE_LONG)
#undef WRITE_LONG
}
} // namespace scr::state::transitions

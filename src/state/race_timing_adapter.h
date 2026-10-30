#pragma once
#include "race/race_timing.h"
#include <algorithm>
#include <array>
#include <cstdint>
namespace scr::state::timing {
inline std::uint16_t word(const std::uint8_t* r,unsigned a){return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a){return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v){r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v){putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v));}
inline RaceTimingInputs readInputs(const std::uint8_t* r){
    RaceTimingInputs in{};in.segmentIndex={r[0x1bb1c],r[0x1bb1d]};in.trackProgress={word(r,0x1bb0a),word(r,0x1bb0c)};
    std::copy_n(r+0x1ecab,256,in.playerNames.begin());in.playerDistanceDifference=word(r,0x1bc3e);
    in.selectedTrackInDivision=r[0x1ca22];in.offTrackStateFlags=r[0x1bb9c];in.finishLineSegment=r[0x1ca1d];
    in.lapLineSegment=r[0x1ca32];in.numberOfTrackSegments=r[0x1ca1a];in.maximumLaps=r[0x1bb99];
    in.currentPlayerId=r[0x5eb79];in.raceStartTimer=r[0x1bbdf];in.wheelMovementActive=r[0x1bb7e];in.raceCompletionCheckFlag=r[0x1bb75];return in;
}
inline void writeInputs(std::uint8_t* r,const RaceTimingInputs& in){
    r[0x1bb1c]=in.segmentIndex[0];r[0x1bb1d]=in.segmentIndex[1];putWord(r,0x1bb0a,in.trackProgress[0]);putWord(r,0x1bb0c,in.trackProgress[1]);
    std::copy(in.playerNames.begin(),in.playerNames.end(),r+0x1ecab);putWord(r,0x1bc3e,in.playerDistanceDifference);
    r[0x1ca22]=in.selectedTrackInDivision;r[0x1bb9c]=in.offTrackStateFlags;r[0x1ca1d]=in.finishLineSegment;
    r[0x1ca32]=in.lapLineSegment;r[0x1ca1a]=in.numberOfTrackSegments;r[0x1bb99]=in.maximumLaps;
    r[0x5eb79]=in.currentPlayerId;r[0x1bbdf]=in.raceStartTimer;r[0x1bb7e]=in.wheelMovementActive;r[0x1bb75]=in.raceCompletionCheckFlag;
}
inline RaceTimingState readState(const std::uint8_t* r){
    RaceTimingState s{};for(unsigned n=0;n<24;++n)s.times[n]={r[0x1c908+n],r[0x1c920+n],r[0x1c938+n]};
    s.crossingArmed={r[0x1bb1e],r[0x1bb1f]};s.lapCounter={r[0x1bb20],r[0x1bb21]};
    s.networkTimeBuffer={r[0x57c6b],r[0x57c6c],r[0x57c6d]};
    std::copy_n(r+0x5ec48,12,s.lapRecordName.begin());std::copy_n(r+0x5ec55,12,s.raceRecordName.begin());
    s.globalFrameCounter=r[0x1bbc9];s.throttleAccumulator=r[0x1bbcf];s.throttleFlag=r[0x1bbcd];s.lapDisplayDuration=r[0x1bbcc];
    s.blinkCountdown=r[0x1bb6c];s.blinkFlag=r[0x1bb88];s.frameBufferSyncMask=r[0x1bb6f];
    s.raceComplete=r[0x1bbaf];s.winnerBits=r[0x1bbb2];s.outcomeFlags=r[0x1bbb4];s.displayFlags=r[0x1bbb1];
    s.bestLapHolder=r[0x1ca23];s.bestRaceHolder=r[0x1ca24];s.messageActive=r[0x1bb8e];s.messageMode=r[0x5e65a];s.messageIndex=r[0x5e65b];
    s.multiplierScratch=r[0x1bb1a];s.productLowScratch=r[0x1bb1b];s.savedPlayerScratch=r[0x1bbe4];
    s.resultsIcon=r[0x1bb8c];s.leaderIcon=r[0x1bbde];s.renderingIndex=r[0x1bbe8];s.subsecondVisibility=r[0x1bb89];return s;
}
inline void writeState(std::uint8_t* r,const RaceTimingState& s){
    for(unsigned n=0;n<24;++n){r[0x1c908+n]=s.times[n].minutes;r[0x1c920+n]=s.times[n].seconds;r[0x1c938+n]=s.times[n].subseconds;}
    r[0x1bb1e]=s.crossingArmed[0];r[0x1bb1f]=s.crossingArmed[1];r[0x1bb20]=s.lapCounter[0];r[0x1bb21]=s.lapCounter[1];
    r[0x57c6b]=s.networkTimeBuffer.minutes;r[0x57c6c]=s.networkTimeBuffer.seconds;r[0x57c6d]=s.networkTimeBuffer.subseconds;
    std::copy(s.lapRecordName.begin(),s.lapRecordName.end(),r+0x5ec48);std::copy(s.raceRecordName.begin(),s.raceRecordName.end(),r+0x5ec55);
    r[0x1bbc9]=s.globalFrameCounter;r[0x1bbcf]=s.throttleAccumulator;r[0x1bbcd]=s.throttleFlag;r[0x1bbcc]=s.lapDisplayDuration;
    r[0x1bb6c]=s.blinkCountdown;r[0x1bb88]=s.blinkFlag;r[0x1bb6f]=s.frameBufferSyncMask;r[0x1bbaf]=s.raceComplete;
    r[0x1bbb2]=s.winnerBits;r[0x1bbb4]=s.outcomeFlags;r[0x1bbb1]=s.displayFlags;r[0x1ca23]=s.bestLapHolder;r[0x1ca24]=s.bestRaceHolder;
    r[0x1bb8e]=s.messageActive;r[0x5e65a]=s.messageMode;r[0x5e65b]=s.messageIndex;r[0x1bb1a]=s.multiplierScratch;
    r[0x1bb1b]=s.productLowScratch;r[0x1bbe4]=s.savedPlayerScratch;r[0x1bb8c]=s.resultsIcon;r[0x1bbde]=s.leaderIcon;
    r[0x1bbe8]=s.renderingIndex;r[0x1bb89]=s.subsecondVisibility;
}
} // namespace scr::state::timing

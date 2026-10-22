#pragma once
#include "opponent_driving.h"
#include "track_progress_adapter.h"
#include <algorithm>
#include <set>

namespace scr::state::opponent {
using Ram=scr::state::memory::ChipRam;
using scr::state::memory::readWord;using scr::state::memory::readLong;using scr::state::memory::writeWord;using scr::state::memory::writeLong;
inline OpponentDrivingData readData(const Ram& r){
    OpponentDrivingData d;d.track=trackProgress::readData(r);
    std::copy_n(r.begin()+0x1c952,100,d.visibility.begin());std::copy_n(r.begin()+0x1ed8a,32,d.traits.begin());
    std::copy_n(r.begin()+0x5be34,16,d.patterns.begin());std::copy_n(r.begin()+0x1e74a,4096,d.distanceFactors.begin());
    for(unsigned i=0;i<3;++i)d.suspensionDamping[i]=readWord(r,0x642ea+2*i);d.lookahead=r[0x63ce0];return d;
}
inline OpponentDrivingInputs readInputs(const Ram& r){return {r[0x1bbc4],r[0x1bb74],r[0x57c3c],r[0x1bbcd],r[0x1ca29],r[0x1bafc],r[0x1bafd],r[0x1bc4b]};}
#define SCR_OPPONENT_BYTES(X) \
 X(randomTail,0x625a6) X(segment,0x1bb1d) X(speedFraction,0x1bb3c) X(enabled,0x1bb44) X(ahead,0x1bbb8) X(drafting,0x1bbc7) \
 X(actionTimer,0x1bbc0) X(patternOffset,0x1bbc2) X(patternControl,0x1bb9d) X(movementOverride,0x1bbbd) \
 X(boundaryCollision,0x1bb45) X(collisionDistance,0x1bb4a) X(lateral,0x1bbed) X(trackDirection,0x1bb40) X(behindSegment,0x1bba6) \
 X(distanceFactor,0x1bb43) X(segmentHalf,0x1bbe6) X(blend,0x1bc41) X(curveSmoothing,0x1bbba) X(trackOffsetBase,0x1bb91) X(scratch1,0x1bb18) X(scratch2,0x1bb19)
#define SCR_OPPONENT_WORDS(X) \
 X(subSegment,0x1bb0c) X(speed,0x1bbee) X(targetSpeed,0x1bbf0) X(minimumBoundary,0x63ede) X(trackDistance,0x1bb12) \
 X(behindProgress,0x1bc24) X(heightDifference,0x1bbf2) X(adjustedDistance,0x1bc4c) X(blendFactor,0x5a54e) X(perpendicular,0x1bb2a) X(attenuated,0x1bb2c) X(distanceOffset,0x1bbec)
inline OpponentDrivingState readState(const Ram& r){OpponentDrivingState s;s.progress=trackProgress::readState(r);s.randomSeed=readLong(r,0x625a2);s.interpolationScratch=readLong(r,0x1bb18);
#define READ_B(f,a) s.f=r[a];
    SCR_OPPONENT_BYTES(READ_B)
#undef READ_B
#define READ_W(f,a) s.f=readWord(r,a);
    SCR_OPPONENT_WORDS(READ_W)
#undef READ_W
    for(unsigned i=0;i<2;++i)s.segmentOffsets[i]=r[0x1bbbe+i];
    for(unsigned i=0;i<3;++i){s.acceleration[i]=readWord(r,0x1bd5e+2*i);s.previousHeight[i]=readWord(r,0x1bd6e+2*i);s.velocity[i]=readWord(r,0x1bd76+2*i);s.wheelForce[i]=readWord(r,0x1bd7e+2*i);}
    for(unsigned i=0;i<4;++i){s.wheelPosition[i]=readWord(r,0x1bd66+2*i);s.bounds[i]=readWord(r,0x1bd86+2*i);s.footprint[i]=readWord(r,0x1bf64+2*i);}
    for(unsigned i=0;i<6;++i)s.controls[i]=readWord(r,0x1bc02+2*i);
    for(unsigned i=0;i<16;++i){s.coordinatesX[i]=readWord(r,0x1bd8e+2*i);s.coordinatesZ[i]=readWord(r,0x1bdae+2*i);}return s;
}
inline void writeState(Ram& r,const OpponentDrivingState& s){trackProgress::writeState(r,s.progress);writeLong(r,0x625a2,s.randomSeed);
#define WRITE_B(f,a) r[a]=s.f;
    SCR_OPPONENT_BYTES(WRITE_B)
#undef WRITE_B
#define WRITE_W(f,a) writeWord(r,a,s.f);
    SCR_OPPONENT_WORDS(WRITE_W)
#undef WRITE_W
    // These overlap shared progress scratch. The caller's final configuration
    // and scratch values, not a stale aggregate long, are authoritative.
    r[0x1bb1a]=s.progress.scratch3;r[0x1bb1b]=s.progress.scratch4;
    for(unsigned i=0;i<2;++i)r[0x1bbbe+i]=s.segmentOffsets[i];
    for(unsigned i=0;i<3;++i){writeWord(r,0x1bd5e+2*i,s.acceleration[i]);writeWord(r,0x1bd6e+2*i,s.previousHeight[i]);writeWord(r,0x1bd76+2*i,s.velocity[i]);writeWord(r,0x1bd7e+2*i,s.wheelForce[i]);}
    for(unsigned i=0;i<4;++i){writeWord(r,0x1bd66+2*i,s.wheelPosition[i]);writeWord(r,0x1bd86+2*i,s.bounds[i]);writeWord(r,0x1bf64+2*i,s.footprint[i]);}
    for(unsigned i=0;i<6;++i)writeWord(r,0x1bc02+2*i,s.controls[i]);
    for(unsigned i=0;i<16;++i){writeWord(r,0x1bd8e+2*i,s.coordinatesX[i]);writeWord(r,0x1bdae+2*i,s.coordinatesZ[i]);}
}
inline void logic(Ram& r,const OpponentDrivingData& d){auto s=readState(r);updateOpponentLogic(d,readInputs(r),s);writeState(r,s);}
inline void geometry(Ram& r,const OpponentDrivingData& d){auto s=readState(r);setupOpponentTrackGeometry(d,readInputs(r),s);writeState(r,s);}
inline void initializeCoordinates(Ram& r,const OpponentDrivingData& d){auto s=readState(r);initializeOpponentCoordinates(d,readInputs(r),s);writeState(r,s);}
#undef SCR_OPPONENT_BYTES
#undef SCR_OPPONENT_WORDS
} // namespace scr::state::opponent

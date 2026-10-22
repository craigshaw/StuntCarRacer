#pragma once
#include "track_progress.h"
#include "track_sampling_adapter.h"
#include <vector>

namespace scr::state::trackProgress {
using Ram=scr::state::memory::ChipRam;
using scr::state::memory::readWord;using scr::state::memory::readLong;using scr::state::memory::writeWord;using scr::state::memory::writeLong;
inline TrackProgressData readData(const Ram& r) {
    TrackProgressData d;d.track=scr::state::memory::decodeTrackGeometry(r);
    for(unsigned i=0;i<256;++i)d.grid[i]=r[0x1c280+i];
    for(unsigned i=0;i<100;++i)d.segmentCoordinates[i]=r[0x1c588+i];
    for(unsigned i=0;i<2048;++i){d.angleTable[i]=readWord(r,0x1cc46+2*i);d.magnitudeTable[i]=readWord(r,0x1dc46+2*i);}
    for(unsigned i=0;i<12;++i)d.perspectiveFactors[i]=r[0x5c6b8+i];return d;
}
inline TrackProgressInputs readInputs(const Ram& r) {
    return {readWord(r,0x1bce4),readWord(r,0x1bce6),readWord(r,0x1bd38),r[0x1bc4b],r[0x1bc45]};
}
#define SCR_PROGRESS_WORDS(X) \
 X(cameraRotation,0x1bc30) X(carHeight,0x1bcd0) X(cameraHeight,0x1bbfa) X(cameraYaw,0x1bc2e) \
 X(baseX,0x1bb22) X(baseZ,0x1bb26) X(trackRotation,0x1bbf2) X(temporaryX,0x1bbf6) X(temporaryZ,0x1bbf8) \
 X(radialAngle,0x1bc1c) X(radialDistance,0x1bc36) X(lateralOrigin,0x1bc5e) X(lateralOffset,0x1bc5c) \
 X(forwardOrigin,0x1bb10) X(targetHeading,0x1bd5a) X(progression,0x1bb0a) X(progressOffset,0x1bb0e) \
 X(visibility,0x1bc26) X(previousVisibility,0x1bc28)
#define SCR_PROGRESS_BYTES(X) \
 X(viewX,0x1bb04) X(viewZ,0x1bb06) X(subGridX,0x1bb07) X(subGridZ,0x1bb09) \
 X(offsetXHigh,0x1bb2e) X(offsetZHigh,0x1bb32) X(transformedGridX,0x65ec0) X(transformedGridZ,0x65ec2) \
 X(gridLookupX,0x1bbd5) X(gridLookupZ,0x1bbd6) X(currentSegment,0x1bb85) X(playerSegment,0x1bb1c) \
 X(lastValidSegment,0x1bb9b) X(temporarySegment,0x1bb19) X(scratch3,0x1bb1a) X(scratch4,0x1bb1b) \
 X(legacyRenderIndex,0x1bbe5) X(renderIndex,0x1bb60) X(depth,0x1bb58) X(previousProgress,0x1bb9f) X(previousDepth,0x1bb9e) \
 X(offTrackFlags,0x1bb9c) X(offRoadSide,0x1bbd1) X(trackSide,0x1bbe1) X(restartCountdown,0x1bb41) X(raceCompletionCheck,0x1bb75)
#define SCR_PROGRESS_CONFIG_WORDS(X) \
 X(primaryOffset,0x1bc8c) X(alternateOffset,0x1bc90) X(descriptorOffset,0x1bcbc) X(primaryBase,0x1bc0e) X(alternateBase,0x1bc10)
#define SCR_PROGRESS_CONFIG_BYTES(X) \
 X(format,0x1bb79) X(alternateFlag,0x1bbdc) X(slopeFlags,0x1bc4a) X(reverseDirection,0x1bc32) X(geometryType,0x1bb86) \
 X(steeringFlags,0x1bb4d) X(limit,0x1bb97) X(limitDoubled,0x1bb59) X(maximum,0x1bb98) X(maximumDoubled,0x1bb5a) \
 X(mode,0x1bb6a) X(directionInversion,0x1bc44) X(primaryOrientation,0x1bb7b) X(alternateOrientation,0x1bbd9) X(steeringBase,0x1bbd4)
inline TrackProgressState readState(const Ram& r) {
    TrackProgressState s;s.segment=scr::state::memory::decodeTrackSamplingState(r).segment;
    for(unsigned i=0;i<3;++i)s.worldPosition[i]=readLong(r,0x1bcd8+4*i);
    s.rotatedX=readLong(r,0x1bccc);s.rotatedZ=readLong(r,0x1bcd4);
#define READ_WORD(field,address) s.field=readWord(r,address);
    SCR_PROGRESS_WORDS(READ_WORD)
#undef READ_WORD
#define READ_BYTE(field,address) s.field=r[address];
    SCR_PROGRESS_BYTES(READ_BYTE)
#undef READ_BYTE
    for(unsigned i=0;i<32;++i)s.debrisY[i]=readWord(r,0x1c3c0+2*i);return s;
}
inline void writeState(Ram& r,const TrackProgressState& s) {
    for(unsigned i=0;i<3;++i)writeLong(r,0x1bcd8+4*i,s.worldPosition[i]);
    writeLong(r,0x1bccc,s.rotatedX);writeLong(r,0x1bcd4,s.rotatedZ);
#define WRITE_WORD(field,address) writeWord(r,address,s.field);
    SCR_PROGRESS_WORDS(WRITE_WORD)
#undef WRITE_WORD
#define WRITE_BYTE(field,address) r[address]=s.field;
    SCR_PROGRESS_BYTES(WRITE_BYTE)
#undef WRITE_BYTE
#define WRITE_CONFIG_WORD(field,address) writeWord(r,address,s.segment.field);
    SCR_PROGRESS_CONFIG_WORDS(WRITE_CONFIG_WORD)
#undef WRITE_CONFIG_WORD
#define WRITE_CONFIG_BYTE(field,address) r[address]=s.segment.field;
    SCR_PROGRESS_CONFIG_BYTES(WRITE_CONFIG_BYTE)
#undef WRITE_CONFIG_BYTE
    for(unsigned i=0;i<32;++i)writeWord(r,0x1c3c0+2*i,s.debrisY[i]);
}
#undef SCR_PROGRESS_WORDS
#undef SCR_PROGRESS_BYTES
#undef SCR_PROGRESS_CONFIG_WORDS
#undef SCR_PROGRESS_CONFIG_BYTES
} // namespace scr::state::trackProgress

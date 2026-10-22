#pragma once
#include "state/track_progress_adapter.h"
#include "track_sampling_fixture.h"
#include <vector>

namespace scr::test::trackProgress {
using namespace scr::state::trackProgress;
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
inline const std::vector<unsigned>& outputBytes() {
    static const std::vector<unsigned> result=[] {
        std::vector<unsigned> v;
#define MARK_WORD(field,address) v.push_back(address);v.push_back(address+1);
        SCR_PROGRESS_WORDS(MARK_WORD) SCR_PROGRESS_CONFIG_WORDS(MARK_WORD)
#undef MARK_WORD
#define MARK_BYTE(field,address) v.push_back(address);
        SCR_PROGRESS_BYTES(MARK_BYTE) SCR_PROGRESS_CONFIG_BYTES(MARK_BYTE)
#undef MARK_BYTE
        for(unsigned a:{0x1bccc,0x1bcd4,0x1bcd8,0x1bce0})for(unsigned i=0;i<4;++i)v.push_back(a+i);
        for(unsigned i=0;i<64;++i)v.push_back(0x1c3c0+i);
        return v;
    }();return result;
}
#undef SCR_PROGRESS_WORDS
#undef SCR_PROGRESS_BYTES
#undef SCR_PROGRESS_CONFIG_WORDS
#undef SCR_PROGRESS_CONFIG_BYTES
} // namespace scr::test::trackProgress

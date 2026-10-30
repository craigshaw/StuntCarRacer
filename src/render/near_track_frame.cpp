#include "render/near_track_frame.h"
#include <stdexcept>

namespace scr {
NearTrackFrame buildNearTrackFrame(const TrackProgressData& data,
    const std::array<std::uint8_t,100>& renderFlags,const NearTrackFrameInputs& inputs,
    NearTrackGeometryState& geometry) {
    if(!data.track.segmentCount||data.track.segmentCount>100)
        throw std::invalid_argument("Near frame requires a loaded track");
    geometry.progress=inputs.progress;geometry.camera=inputs.camera;
    geometry.displayUpdate=inputs.displayUpdate;
    geometry.startIndex=0;geometry.renderingLoopIndex=inputs.progress.renderIndex;
    geometry.continuation=geometry.viewReverse=0;
    geometry.processedIndices={0x80,0x80};
    geometry.progress.currentSegment=inputs.progress.playerSegment;
    const auto advance=[&](){geometry.progress.currentSegment=std::uint8_t((unsigned(geometry.progress.currentSegment)+1)%data.track.segmentCount);};
    if(geometry.progress.progressOffset&0x8000){advance();geometry.progress.progressOffset&=0xff;}
    NearTrackFrame result;
    result.queue.horizontalPadding=geometry.horizontalPadding;result.queue.topPadding=geometry.topPadding;result.queue.bottomPadding=geometry.bottomPadding;
    if(expandedTrackView(geometry.horizontalPadding,geometry.topPadding,geometry.bottomPadding))
        result.queue.entries.resize(92); // Three dense passes, plus boundary/sentinel slots.
    result.queue.opponentSegment=inputs.opponentSegment;
    result.queue.opponentSubSegment=inputs.opponentSubSegment;
    result.queue.minimumBoundaryDistance=inputs.minimumBoundaryDistance;
    if(!(geometry.progress.progressOffset>>8)){
        const unsigned previous=(unsigned(geometry.progress.currentSegment)+data.track.segmentCount-1)%data.track.segmentCount;
        if(previous==inputs.opponentSegment)result.queue.opponentOffset=0;
    }
    for(unsigned pass=0;pass<3;++pass){
        projectNearTrackSegment(data,geometry);
        processNearTrackVisibility(geometry,renderFlags.at(geometry.progress.currentSegment),result.queue,
            [&](std::uint8_t index){materializeNearTrackLower(data,geometry,index);});
        if(!pass){const auto cuts=prepareNearTrackCutEdges(data,geometry);seedNearTrackCutEdges(geometry,cuts,result.queue);}
        appendNearTrackSegment(geometry,result.queue,pass==0);
        // Cull the coarse far mesh for every visited near segment, including
        // sections whose original visibility rules emitted no drawable face.
        result.presentation.coveredSegments.at(geometry.progress.currentSegment)=true;
        if(!pass){geometry.renderingLoopIndex=0;geometry.startIndex=4;}
        (void)shiftNearTrackCoordinates(geometry);advance();
        if(pass==1)geometry.continuation=1;
    }
    result.presentation.queueEntries=std::uint16_t((result.queue.offset-32)/32);
    result.presentation.opponentOffset=result.queue.opponentOffset;
    result.presentation.farStartSegment=geometry.progress.currentSegment;
    result.presentation.commands=drawNearTrackQueue(result.queue,inputs.drawing);
    return result;
}
} // namespace scr

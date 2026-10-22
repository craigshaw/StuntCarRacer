#pragma once
#include "near_track_geometry.h"
#include "track_progress_adapter.h"
#include "render_projection_adapter.h"

namespace scr::state::nearGeometry {
using Ram=scr::state::memory::ChipRam;
inline NearTrackGeometryState readState(const Ram& r){
    NearTrackGeometryState s;s.progress=trackProgress::readState(r);
    s.camera.heightBaseline=s.progress.cameraHeight;s.camera.yawResidual=s.progress.cameraYaw;
    s.camera.viewportCenterY=scr::state::memory::readWord(r,0x1bc42);s.camera.sinRoll=scr::state::memory::readWord(r,0x1c250);s.camera.cosRoll=scr::state::memory::readWord(r,0x1c252);
    for(unsigned n=0;n<60;++n){
        for(unsigned lower=0;lower<2;++lower){auto& p=lower?s.lower[n]:s.upper[n];const auto index=n*2+lower*120;
            p.height=scr::state::memory::readWord(r,0x1be70+index);p.x=renderProjection::signedWord(scr::state::memory::readWord(r,0x1bfb0+index));p.y=renderProjection::signedWord(scr::state::memory::readWord(r,0x1c0f0+index));}
    }
    for(unsigned n=0;n<30;++n)s.flags[n]=r[0x1bdd0+n*4];
    for(unsigned n=0;n<2;++n){s.processedIndices[n]=r[0x1bc14+n*2];s.savedHeights[n]=scr::state::memory::readWord(r,0x1bb34+n*2);s.transformedCoordinates[n]=scr::state::memory::readWord(r,0x1c0a0+n*2);}
    s.startIndex=r[0x1bbc5];s.renderingLoopIndex=r[0x1bb60];s.viewReverse=r[0x1bbae];s.continuation=r[0x1bb7f];s.edgeVisibilityMode=r[0x1bb68];
    s.displayUpdate=r[0x5d724];s.gridSweepDirection=r[0x1bb93];s.lapApproachSegment=r[0x1ca1c];s.preview=r[0x1bb8f];s.segmentHalfFlags=r[0x1bbe6];
    s.heightDifference=r[0x1bbf2];s.trackOffsetBase=r[0x1bb91];s.processingIndex=r[0x1bb7a];s.lastProcessed=r[0x654c0];s.reverseDecision=r[0x1bbad];s.perspectiveDepth=scr::state::memory::readWord(r,0x1bc36);
    return s;
}
inline void writeState(Ram& r,const NearTrackGeometryState& s){
    trackProgress::writeState(r,s.progress);
    scr::state::memory::writeWord(r,0x1bc42,s.camera.viewportCenterY);scr::state::memory::writeWord(r,0x1c250,s.camera.sinRoll);scr::state::memory::writeWord(r,0x1c252,s.camera.cosRoll);
    for(unsigned n=0;n<60;++n)for(unsigned lower=0;lower<2;++lower){const auto& p=lower?s.lower[n]:s.upper[n];const auto index=n*2+lower*120;
        scr::state::memory::writeWord(r,0x1be70+index,p.height);scr::state::memory::writeWord(r,0x1bfb0+index,std::uint16_t(p.x));scr::state::memory::writeWord(r,0x1c0f0+index,std::uint16_t(p.y));}
    for(unsigned n=0;n<30;++n)r[0x1bdd0+n*4]=s.flags[n];
    for(unsigned n=0;n<2;++n){r[0x1bc14+n*2]=s.processedIndices[n];scr::state::memory::writeWord(r,0x1bb34+n*2,s.savedHeights[n]);scr::state::memory::writeWord(r,0x1c0a0+n*2,s.transformedCoordinates[n]);}
    r[0x1bbc5]=s.startIndex;r[0x1bb60]=s.renderingLoopIndex;r[0x1bbae]=s.viewReverse;r[0x1bb7f]=s.continuation;r[0x1bb68]=s.edgeVisibilityMode;
    r[0x5d724]=s.displayUpdate;r[0x1bb93]=s.gridSweepDirection;r[0x1ca1c]=s.lapApproachSegment;r[0x1bb8f]=s.preview;r[0x1bbe6]=s.segmentHalfFlags;
    r[0x1bbf2]=s.heightDifference;r[0x1bb91]=s.trackOffsetBase;r[0x1bb7a]=s.processingIndex;r[0x654c0]=s.lastProcessed;r[0x1bbad]=s.reverseDecision;scr::state::memory::writeWord(r,0x1bc36,s.perspectiveDepth);
}
} // namespace scr::state::nearGeometry

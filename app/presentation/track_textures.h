#pragma once
#include "render/near_track_frame.h"

namespace scr::runtime {
// Optional prototype: add material coordinates to a private command copy,
// preserving original fills, clipping, painter order and line allocation.
NearTrackPresentation textureTrackPresentation(const NearTrackPresentation&,
    const TrackProgressData&,const RenderCameraInputs&,const RenderProjectionCamera&);
} // namespace scr::runtime

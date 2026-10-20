#pragma once
#include "near_track_visibility.h"

namespace scr {
struct NearTrackFrameInputs {
    TrackProgressState progress;
    RenderProjectionCamera camera;
    NearTrackDrawInputs drawing;
    std::uint8_t displayUpdate{},opponentSegment{},opponentSubSegment{};
    std::uint16_t minimumBoundaryDistance{};
};
struct NearTrackPresentation {
    std::vector<NearTrackDrawCommand> commands;
    std::array<bool,100> coveredSegments{};
    std::uint16_t queueEntries{};
    std::uint16_t opponentOffset{0xffff};
    std::uint8_t farStartSegment{};
};
struct NearTrackFrame {
    NearTrackQueueState queue;
    NearTrackPresentation presentation;
};
// Original frame's three dense segment passes, before the far strip.
// Keep geometry across calls: the saved unrolled endpoint pair is history used
// by the next frame. Refresh gameplay inputs without writing them back.
NearTrackFrame buildNearTrackFrame(const TrackProgressData&,
    const std::array<std::uint8_t,100>& segmentRenderFlags,
    const NearTrackFrameInputs&,NearTrackGeometryState& geometry);
} // namespace scr

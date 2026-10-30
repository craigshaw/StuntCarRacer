#pragma once
#include "render/near_track_frame.h"
#include "render/track_mesh.h"
#include "render/mountain_horizon.h"
namespace scr {
struct OpponentFarTrackData {
    TrackMeshData mesh;
    std::array<std::uint8_t,100> projectionLimits{};
};
struct OpponentFarTrackFrame {
    NearTrackGeometryState geometry;
    NearTrackQueueState queue;
    std::vector<std::array<std::uint8_t,2>> headers; // flags,segment
    // Only the first original 60 vertices are retained in geometry/history.
    // Expanded queries can project beyond that original scratch buffer.
    std::vector<NearTrackVertex> upper,lower;
    std::uint16_t denseEnd{};
    std::uint16_t vertexEnd{};
};
// Original65ec4 and67460, starting at the carry left by the three dense passes.
// The returned queue includes dense entries and their shared line allocation.
OpponentFarTrackFrame buildOpponentFarTrack(const OpponentFarTrackData&,
    const RenderProjectionTables&,const NearTrackGeometryState&,const NearTrackQueueState&);
// Original690bc painter, before the original near painter. Consuming an
// opponent marker here prevents its later admission in a near-track slot.
std::vector<NearTrackDrawCommand> drawOpponentFarTrack(const OpponentFarTrackFrame&,
    const NearTrackDrawInputs&,const NearTrackPainter& painter={});
// Original67888 and6953e allocate into the same arena between road queue
// construction and car drawing. The horizon's single line bypasses the usual
// allocation guard; selected mountain template edges retain that guard.
std::uint16_t allocateOpponentBackgroundLines(std::uint16_t lineBytes,
    const MountainHorizonData&,std::uint16_t yaw,const RenderProjectionCamera&,
    std::uint8_t edgeVisibilityMode,NearTrackGeometryState* history=nullptr);
} // namespace scr

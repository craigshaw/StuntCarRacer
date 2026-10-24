#include "replay_scene_commands.h"
#include <utility>

namespace scr::viewer {
ReplaySceneCommands buildReplaySceneCommands(const NearTrackFrame& near,
    const NearTrackDrawInputs& drawing,const RenderProjectionTables& tables,
    const OpponentFarTrackData& farData,const NearTrackGeometryState& geometry,
    const MountainHorizonData& mountains,std::uint16_t yaw,
    const RenderProjectionCamera& camera,
    const std::optional<OpponentCarInputs>& opponent){
    ReplaySceneCommands result;
    result.presentation=near.presentation;
    result.denseLineBytes=near.queue.lineBytes;
    const auto far=buildOpponentFarTrack(farData,tables,geometry,near.queue);
    result.geometry=far.geometry;
    result.farLineBytes=far.queue.lineBytes;
    result.horizonMode=result.geometry.edgeVisibilityMode;
    result.presentation.horizonMode=result.horizonMode;
    auto lineBytes=allocateOpponentBackgroundLines(far.queue.lineBytes,mountains,
        yaw,camera,result.horizonMode,&result.geometry);
    result.backgroundLineBytes=lineBytes;
    auto marker=opponent?far.queue.opponentOffset:std::uint16_t{0xffff};
    const NearTrackPainter painter=[&](NearTrackPainterSlot slot,std::uint16_t offset,
        std::vector<NearTrackDrawCommand>& commands){
        // The original frame clears the marker in Practice (0x65096). Local
        // competitive races use the occluded slot (0x58600).
        if(!consumeOpponentCarPainterSlot(static_cast<OpponentCarPainterSlot>(slot),
            0,offset,marker)||!opponent)return;
        auto inputs=*opponent;inputs.camera=camera;inputs.lineBytes=lineBytes;inputs.horizontalPadding=geometry.horizontalPadding;inputs.topPadding=geometry.topPadding;inputs.bottomPadding=geometry.bottomPadding;
        const auto car=buildOpponentCarFrame(inputs,tables);
        result.opponentAttempted=true;result.opponentAdmitted=car.admitted;
        lineBytes=car.lineBytes;
        commands.insert(commands.end(),car.commands.begin(),car.commands.end());
    };
    auto commands=drawOpponentFarTrack(far,drawing,painter);
    auto dense=drawNearTrackQueue(near.queue,drawing,painter,&lineBytes);
    commands.insert(commands.end(),dense.begin(),dense.end());
    result.presentation.commands=std::move(commands);
    result.presentation.coveredSegments.fill(true);
    result.remainingOpponentOffset=marker;
    result.lineBytes=lineBytes;
    return result;
}
} // namespace scr::viewer

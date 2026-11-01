#include "presentation/scene_commands.h"
#include <stdexcept>
#include <utility>

namespace scr::runtime {
NearTrackPresentation refineGhostTrackDepth(const NearTrackPresentation& painted,
    const TrackProgressData& data,const RenderCameraInputs& pose,const RenderProjectionCamera& camera){
    using Patches=std::vector<std::array<RenderCameraRelative,4>>;
    std::array<std::array<Patches,3>,100> patches;
    std::array<bool,100> ready{};
    auto result=painted;
    for(auto& command:result.commands){
        if(command.kind!=NearTrackCommandKind::fill||command.surface==NearTrackSurface::opponent||command.depthSegments.empty())continue;
        command.depthPatches.clear();
        for(const auto segment:command.depthSegments){
            if(segment>=data.track.segmentCount)throw std::out_of_range("Ghost depth references an unloaded segment");
            if(!ready[segment]){
                TrackProgressState sourceCamera;sourceCamera.worldPosition=pose.worldPosition;
                updateTrackCamera({pose.pitch,pose.yaw,pose.averageWheelVelocity},sourceCamera);
                sourceCamera.cameraHeight=camera.heightBaseline;
                const auto groundWord=std::uint16_t(int(camera.heightBaseline)-512);
                const auto ground=(groundWord<0x8000?int(groundWord):int(groundWord)-65536)/64.0;
                for(const auto& road:buildDetailedTrackDepth(data,sourceCamera,segment)){
                    const auto a=road[0],b=road[1],c=road[2],d=road[3];
                    patches[segment][unsigned(NearTrackSurface::road)].push_back({a,b,c,d});
                    auto lowA=a,lowB=b,lowC=c,lowD=d;lowA.down=lowB.down=lowC.down=lowD.down=ground;
                    patches[segment][unsigned(NearTrackSurface::rightSide)].push_back({a,lowA,lowD,d});
                    patches[segment][unsigned(NearTrackSurface::leftSide)].push_back({b,lowB,lowC,c});
                }
                ready[segment]=true;
            }
            const auto& source=patches[segment][unsigned(command.surface)];
            command.depthPatches.insert(command.depthPatches.end(),source.begin(),source.end());
        }
    }
    return result;
}
SceneCommands buildSceneCommands(const NearTrackFrame& near,
    const NearTrackDrawInputs& drawing,const RenderProjectionTables& tables,
    const OpponentFarTrackData& farData,const NearTrackGeometryState& geometry,
    const MountainHorizonData& mountains,std::uint16_t yaw,
    const RenderProjectionCamera& camera,
    const std::optional<OpponentCarInputs>& opponent){
    SceneCommands result;
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
} // namespace scr::runtime

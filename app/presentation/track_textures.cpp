#include "presentation/track_textures.h"
#include <stdexcept>

namespace scr::runtime {
NearTrackPresentation textureTrackPresentation(const NearTrackPresentation& painted,
    const TrackProgressData& data,const RenderCameraInputs& pose,const RenderProjectionCamera& camera){
    using Patches=std::vector<TrackTexturePatch>;
    std::array<std::array<Patches,3>,100> patches;
    std::array<bool,100> ready{};
    TrackProgressState sourceCamera;sourceCamera.worldPosition=pose.worldPosition;
    updateTrackCamera({pose.pitch,pose.yaw,pose.averageWheelVelocity},sourceCamera);
    sourceCamera.cameraHeight=camera.heightBaseline;
    const auto groundWord=std::uint16_t(int(camera.heightBaseline)-512);
    const auto ground=(groundWord<0x8000?int(groundWord):int(groundWord)-65536)/64.0;
    auto result=painted;
    for(auto& command:result.commands){
        command.texturePatches.clear();
        if(command.kind!=NearTrackCommandKind::fill||command.surface==NearTrackSurface::opponent)continue;
        for(const auto segment:command.depthSegments){
            if(segment>=data.track.segmentCount)throw std::out_of_range("Texture references an unloaded segment");
            if(!ready[segment]){
                const auto roads=buildDetailedTrackDepth(data,sourceCamera,segment);
                for(unsigned i=0;i<roads.size();++i){
                    const auto& road=roads[i];
                    // Integer repeats per segment close the circuit without a
                    // seam. Section identity remains fixed when the bridge moves.
                    const auto first=segment*8.0+8.0*i/roads.size(),last=segment*8.0+8.0*(i+1)/roads.size();
                    patches[segment][unsigned(NearTrackSurface::road)].push_back({road,{{{0,first},{1,first},{1,last},{0,last}}}});
                    for(unsigned side=0;side<2;++side){
                        const auto a=road[side],b=road[3-side];auto lowA=a,lowB=b;lowA.down=lowB.down=ground;
                        // Vertical texture coordinates use height above the fixed
                        // ground plane, with equal physical scale on both sides.
                        patches[segment][side].push_back({{{a,lowA,lowB,b}},{{{(ground-a.down)/8,first},{0,first},{0,last},{(ground-b.down)/8,last}}}});
                    }
                }
                ready[segment]=true;
            }
            const auto& source=patches[segment][unsigned(command.surface)];
            command.texturePatches.insert(command.texturePatches.end(),source.begin(),source.end());
        }
    }
    return result;
}
} // namespace scr::runtime

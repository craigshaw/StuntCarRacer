#include "assets/native_startup.h"
#include "race/race_scene.h"
#include "state/track_mesh_adapter.h"
#include "state/track_progress_adapter.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool hasFarLines(const scr::OpponentFarTrackFrame& frame){
    for(unsigned at=frame.denseEnd/32;at<frame.queue.offset/32;++at)
        for(const auto& edge:frame.queue.entries.at(at).edges)if(edge.present)return true;
    return false;
}
void rightEdgeWall(const std::vector<std::uint32_t>& pixels,const RaceRenderAssets& assets,unsigned alternate){
    require(pixels.size()==864*200,"Stepping Stones reproduction has the wrong canvas size");
    // In the initial drop the red/white track wall crosses every outer column.
    // The original 29-section cap left all of these columns without track.
    for(unsigned x=800;x<864;++x){
        bool wall=false;
        for(unsigned y=10;y<100;++y)
            wall|=pixels[y*864+x]==assets.palette[15]||pixels[y*864+x]==assets.palette.at(alternate);
        require(wall,"Stepping Stones wall stops before the right edge");
    }
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: native_track_visibility_tests assets output");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    auto seed=startNativeRace(argv[1],1); // Practice on Stepping Stones, initial car drop.
    const auto& memory=*seed.memory;
    const auto data=scr::state::trackProgress::readData(memory);
    const auto assets=decodeRaceRenderAssets(seed.payload.data(),seed.payload.size(),memory.data(),memory.size());
    scr::OpponentFarTrackData farData;farData.mesh=scr::state::mesh::decodeTrackMesh(memory);
    std::copy_n(memory.begin()+0x65e70,100,farData.projectionLimits.begin());
    std::array<std::uint8_t,100> flags{};std::array<bool,100> known{};
    for(const auto& section:farData.mesh.sections)if(!known[section.segment]){flags[section.segment]=section.flags;known[section.segment]=true;}
    scr::RenderProjectionTables tables;tables.angleTable=data.angleTable;tables.magnitudeTable=data.magnitudeTable;
    RaceScene scene;scene.loadNativeState(memory,seed.payload,{});
    unsigned pixelChecks=0,projectionChecks=0;
    for(unsigned tick=0;tick<=16;++tick){
        if(tick%8==0){
            const auto& frame=scene.frames().back();
            const auto original=scene.renderLive(0,false,false);
            for(bool interpolate:{false,true}){
                rightEdgeWall(scene.renderLive(0.5,false,interpolate,NativeView::Ultrawide),assets,frame.nearInputs.drawing.alternateSideColor);
                ++pixelChecks;
            }
            scene.renderLive(0,false,false,NativeView::Ultrawide);
            scene.saveBitmap(output/("stepping-stones-drop-"+std::to_string(tick)+".bmp"));
            require(scene.renderLive(0,false,false)==original,"Expanded query changed the retained Original frame");
            for(bool expanded:{false,true}){
                auto geometry=frame.nearSeed;
                if(expanded){geometry.horizontalPadding=304;geometry.topPadding=16;geometry.bottomPadding=56;}
                const auto near=scr::buildNearTrackFrame(data,flags,frame.nearInputs,geometry);
                const auto far=scr::buildOpponentFarTrack(farData,tables,geometry,near.queue);
                if(expanded){
                    require(far.vertexEnd>120&&far.queue.offset>0x5e0,"Wide view still stops at the original coordinate/queue capacity");
                    const auto last=unsigned(far.vertexEnd)/2-2;
                    require(!scr::nearTrackInsideX(far.upper.at(last).x,304)&&!scr::nearTrackInsideX(far.upper.at(last+1).x,304),"Far track stopped while its upper edge was still visible");
                    auto limited=farData;limited.projectionLimits.fill(80);
                    require(scr::buildOpponentFarTrack(limited,tables,geometry,near.queue).vertexEnd<=80,"Expanded projection lost authored shorter cuts");
                    if(!tick){
                        // Exercise index values beyond a byte and a cache which
                        // never leaves the viewport. Projection must terminate.
                        auto circuit=farData;
                        const auto first=*std::lower_bound(circuit.mesh.sections.begin(),circuit.mesh.sections.end(),geometry.progress.currentSegment,
                            [](const scr::TrackMeshSection& section,unsigned segment){return section.segment<segment;});
                        circuit.mesh.sections.assign(80,first);circuit.projectionLimits.fill(120);
                        const auto loop=scr::buildOpponentFarTrack(circuit,tables,geometry,near.queue);
                        require(loop.vertexEnd==4*(circuit.mesh.sections.size()+1),"Expanded projection did not terminate after one cache circuit");
                        require(loop.queue.offset==near.queue.offset+32*circuit.mesh.sections.size(),"Expanded queue truncated an in-view cache circuit");
                    }
                }else{
                    require(far.vertexEnd<=120&&far.queue.offset<=0x5e0&&far.queue.entries.size()==48,"Original projection lost its recovered limits");
                }
                auto full=near.queue;full.lineBytes=9000;
                require(hasFarLines(scr::buildOpponentFarTrack(farData,tables,geometry,full))==expanded,"Expanded line storage still shares the Original 9000-byte ceiling");
                ++projectionChecks;
            }
        }
        if(tick<16)require(scene.stepLive(0),"Initial-drop reproduction unexpectedly ended");
    }
    std::cout<<"{\"status\":\"passed\",\"stepping_stones_pixel_checks\":"<<pixelChecks
        <<",\"projection_checks\":"<<projectionChecks<<",\"original_limits_preserved\":true,\"authored_cuts_preserved\":true,\"cache_circuit_bounded\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

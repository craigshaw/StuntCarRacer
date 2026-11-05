#include "assets/native_startup.h"
#include "race/race_scene.h"
#include "presentation/track_textures.h"
#include "state/track_progress_adapter.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr;
using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Hash {
    std::uint64_t value=14695981039346656037ULL;
    template<class T>void add(const T& v){const auto* bytes=reinterpret_cast<const unsigned char*>(&v);for(unsigned i=0;i<sizeof(T);++i){value^=bytes[i];value*=1099511628211ULL;}}
};
std::uint64_t retainedHash(const RaceScene& scene){
    Hash h;
    for(const auto& frame:scene.frames()){
        h.add(frame.pose);h.add(frame.camera);h.add(frame.nearInputs);h.add(frame.nearSeed);h.add(frame.cockpit);h.add(frame.instrumentPixels);
        for(const auto& command:frame.nearTrack.commands){
            require(command.texturePatches.empty(),"Prototype UVs leaked into retained commands");
            h.add(command.kind);h.add(command.surface);h.add(command.color);h.add(command.originQueueOffset);h.add(command.firstQueueOffset);
            for(const auto& edge:command.edges)h.add(edge);
            for(const auto& patch:command.depthPatches)h.add(patch);
            for(auto segment:command.depthSegments)h.add(segment);
        }
        if(frame.movingTrack)for(auto byte:frame.movingTrack->progress.track.geometry)h.add(byte);
    }
    return h.value;
}
void samplerChecks(){
    for(bool road:{false,true}){
        const TrackTextureCoordinate uv{0.32,0.71},dx{0.0001,0},dy{0,0.0001};
        require(sampleTrackMaterial(road,0xaabbcc,uv,dx,dy)==sampleTrackMaterial(road,0xaabbcc,{uv.u,uv.v+8},dx,dy),"Texture has a longitudinal repeat seam");
    }
    require(sampleTrackMaterial(false,0xffffff,{0.1,0.3},{2,0},{0,2})==sampleTrackMaterial(false,0xffffff,{0.7,0.9},{2,0},{0,2}),"Distant panel texture does not converge to its mip average");
    require(sampleTrackMaterial(true,0xaaaa88,{0.32,0.71},{0.001,0},{0,0.001})!=sampleTrackMaterial(true,0xaaaa88,{0.37,0.73},{0.001,0},{0,0.001}),"Asphalt has no spatial detail");
}
unsigned coverageCheck(RaceScene& scene,const NativeStartupData& startup){
    const auto& frame=scene.frames().back();
    const auto data=frame.movingTrack?frame.movingTrack->progress:scr::state::trackProgress::readData(*startup.memory);
    const auto textured=textureTrackPresentation(frame.nearTrack,data,frame.pose,frame.camera);
    RenderProjectionTables tables;tables.angleTable=data.angleTable;tables.magnitudeTable=data.magnitudeTable;
    std::array<std::uint32_t,16> palette{};for(unsigned n=0;n<palette.size();++n)palette[n]=0x101010*(n+1);
    SoftwareRenderer renderer;std::vector<std::uint32_t> a(320*200),b(320*200);
    const auto base=renderer.drawFrame({}, {}, {},frame.camera,tables,palette,a,false,&frame.nearTrack);
    const auto coverage=renderer.worldCoverage();
    const auto changed=renderer.drawFrame({}, {}, {},frame.camera,tables,palette,b,false,&textured);
    require(coverage==renderer.worldCoverage(),"Texturing changed track/opponent silhouette or painter order");
    require(base.nearFillCommands==changed.nearFillCommands&&base.edgeHighlightPixels==changed.edgeHighlightPixels,"Texturing changed fill/edge admission");
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x){
        if(coverage[y*256+x]!=1&&coverage[y*256+x]!=2)
            require(a[(y+16)*320+x+32]==b[(y+16)*320+x+32],"Texturing changed a sky, ground, mountain or opponent pixel");
    }
    return changed.trackTextureSamples;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: native_track_texture_tests assets output");
    samplerChecks();
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    unsigned scenes=0,queries=0,changedFrames=0,coverageSamples=0,bridgeChanges=0;
    double renderMilliseconds=0;unsigned timedRenders=0;
    for(unsigned track=0;track<8;++track){
        const auto previousChanged=changedFrames;
        const auto startup=startNativeRace(argv[1],track);
        const auto assets=decodeRaceRenderAssets(startup.payload.data(),startup.payload.size(),startup.memory->data(),startup.memory->size());
        const auto sprites=decodeNativeWheelSprites(startup.memory->data(),startup.memory->size());
        auto viewed=std::make_unique<RaceScene>(),untouched=std::make_unique<RaceScene>();
        viewed->loadNativeState(*startup.memory,startup.payload,{});untouched->loadNativeState(*startup.memory,startup.payload,{});
        std::uint8_t lastPhase=0;
        for(unsigned tick=0;tick<256;++tick){
            if(track==5&&viewed->frames().back().movingTrack){const auto phase=viewed->frames().back().movingTrack->bridgePhase;bridgeChanges+=phase!=lastPhase;lastPhase=phase;}
            if(tick==16||tick==64||tick==128||tick==255){
                const auto before=retainedHash(*viewed);
                coverageSamples+=coverageCheck(*viewed,startup);
                for(auto aperture:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide}){
                    CameraViewSettings original;original.view=aperture;
                    auto textured=original;textured.texturedTrack=true;
                    for(double fraction:{0.0,0.5,1.0}){
                        const auto baseline=viewed->renderView(fraction,original);
                        const auto start=std::chrono::steady_clock::now();
                        const auto enhanced=viewed->renderView(fraction,textured);
                        renderMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++timedRenders;
                        changedFrames+=enhanced!=baseline;
                        require(enhanced==viewed->renderView(fraction,textured),"Texturing depends on query order");
                        require(baseline==viewed->renderView(fraction,original),"Texture toggle failed to restore the original frame");
                        queries+=3;
                        if(aperture==NativeView::Original){
                            for(unsigned y=144;y<200;++y)for(unsigned x=0;x<320;++x)
                                require(baseline[y*320+x]==enhanced[y*320+x],"Texturing changed the original dashboard");
                        }
                        if(aperture==NativeView::Widescreen&&fraction==0.5&&(tick==64||tick==128)){
                            const auto stem="track-"+std::to_string(track)+"-tick-"+std::to_string(tick);
                            viewed->renderView(fraction,original);viewed->saveBitmap(output/(stem+"-original.bmp"));
                            viewed->renderView(fraction,textured);viewed->saveBitmap(output/(stem+"-textured.bmp"));
                        }
                    }
                }
                require(before==retainedHash(*viewed),"Texturing modified saved frames or moving-track data");
                // Exact gameplay poses still animate wheels at 50 Hz. A material
                // toggle must preserve that clock when smoothing is disabled.
                for(double fraction:{0.25,0.75}){
                    const auto original=viewed->renderLive(fraction,false,false);
                    const auto enhanced=viewed->renderLive(fraction,false,false,NativeView::Original,true);
                    const auto& frame=viewed->frames().back();
                    std::vector<std::uint32_t> foreground(320*200,transparentPixel);
                    composeCockpit(assets,frame.cockpit,foreground);
                    composeNativeWheelSprites(sprites,frame.wheelSprites,frame.wheelTimeline[unsigned(fraction*6)],foreground);
                    for(unsigned n=0;n<foreground.size();++n)if(foreground[n]!=transparentPixel)
                        require(original[n]==enhanced[n],"Texture toggle changed cockpit artwork or the 50 Hz wheel phase");
                    require(viewed->renderLive(fraction,false,false)==original,"Exact live texture toggle failed to restore its frame");
                    queries+=3;
                }
            }
            if(track==0&&tick>=112&&tick<160){
                const auto motion=output/"motion";std::filesystem::create_directories(motion);
                CameraViewSettings settings;settings.view=NativeView::Widescreen;
                for(unsigned half=0;half<2;++half){
                    const auto stem=std::to_string((tick-112)*2+half);
                    settings.texturedTrack=false;viewed->renderView(half*0.5,settings);viewed->saveBitmap(motion/(stem+"-original.bmp"));
                    settings.texturedTrack=true;viewed->renderView(half*0.5,settings);viewed->saveBitmap(motion/(stem+"-textured.bmp"));
                    queries+=2;
                }
            }
            const auto controls=tick<200?1u:2u;
            require(viewed->stepLive(controls)==untouched->stepLive(controls),"Graphics changed race progression");
            require(viewed->renderLive(0,false,false)==untouched->renderLive(0,false,false),"Graphics changed a later baseline frame");
        }
        require(changedFrames>previousChanged,"A track never displayed the texture prototype");
        viewed->quitLive();untouched->quitLive();
        auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();
        viewed->copyLiveExit(*a);untouched->copyLiveExit(*b);
        require(*a==*b,"Texture queries changed simulation or RNG memory");++scenes;
    }
    require(changedFrames>0&&coverageSamples>0,"Prototype did not visibly texture track surfaces");
    require(bridgeChanges>0,"Prototype omitted moving Draw Bridge geometry");
    std::cout<<"{\"status\":\"passed\",\"scenes\":"<<scenes<<",\"queries\":"<<queries<<",\"changed_frames\":"<<changedFrames
        <<",\"texture_samples\":"<<coverageSamples<<",\"bridge_changes\":"<<bridgeChanges<<",\"mean_textured_render_ms\":"<<renderMilliseconds/timedRenders
        <<",\"simulation_rng_isolated\":true,\"original_coverage_preserved\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

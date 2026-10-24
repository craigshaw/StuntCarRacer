#include "native_startup.h"
#include "replay_scene.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool worldPixel(unsigned x,unsigned y){return x>=32&&x<288&&y>=16&&y<144;}
void compareArtwork(const std::vector<std::uint32_t>& original,const std::vector<std::uint32_t>& extended,
    const ReplayRenderAssets& assets,const CockpitFrame& cockpit,const NativeWheelSprites& sprites,
    const NativeWheelSpriteFrame& placements,const scr::WheelAnimationState& animation){
    require(original.size()==320*200&&extended.size()==original.size(),"Extended changed the original canvas dimensions");
    std::vector<std::uint32_t> foreground(320*200,replayTransparent);
    composeReplayCockpit(assets,cockpit,foreground,true);composeNativeWheelSprites(sprites,placements,animation,foreground);
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){const unsigned at=y*320+x;
        if(y==83&&(x==16||x==303)) {
            require(original[at]==assets.palette[0]&&extended[at]==assets.palette[9],"Extended did not close just the original post/brace seam");
            continue;
        }
        if(foreground[at]!=replayTransparent||(!worldPixel(x,y)&&!assets.exteriorMask[at]))
            require(extended[at]==original[at],"Extended moved/replaced an original cockpit, wheel, exhaust or instrument pixel");
    }
}
void maskTests(){
    scr::IndexedImage image;image.width=320;image.height=200;image.pixels.assign(320*200,10);
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<3;++x){image.pixels[y*320+x]=0;image.pixels[y*320+319-x]=0;}
    for(unsigned x=0;x<36;++x){image.pixels[x]=0;image.pixels[319-x]=0;}
    image.pixels[110*320+10]=0;image.pixels[180*320+100]=0;image.pixels[30*320+100]=0;
    image.pixels[100]=0; // An enclosed black roof/damage detail is still opaque.
    image.pixels[83*320+16]=0;image.pixels[83*320+17]=9;
    image.pixels[83*320+303]=0;image.pixels[83*320+302]=9;
    const auto mask=makeReplayExteriorMask(image);
    require(std::count(mask.begin(),mask.end(),1)==186,"Exterior mask did not follow just the connected upper corners");
    require(mask[32]&&mask[35]&&mask[284]&&mask[287],"Exterior mask stopped at the old damage-strip boundary");
    require(!mask[100]&&!mask[110*320+10]&&!mask[180*320+100]&&!mask[30*320+100],"Exterior mask made a roof, tyre, instrument or interior black transparent");
    ReplayRenderAssets assets;assets.background=image;assets.exteriorMask=mask;
    for(unsigned i=0;i<16;++i)assets.palette[i]=i;
    std::vector<std::uint32_t> pixels(320*200,0x234567);composeReplayFrame(assets,pixels);
    require(pixels[32]==0x234567&&pixels[287]==0x234567&&pixels[100]==0,"Extended roof masking changed enclosed black details");
    require(pixels[83*320+16]==9&&pixels[83*320+303]==9,"Extended seam repair did not use the adjacent dark-red outline");
    makeReplayBackground(assets,pixels);
    require(pixels[83*320+16]==0&&pixels[83*320+303]==0,"Extended seam repair mutated the Original bitmap");
}
void roofMaskTests(const ReplayRenderAssets& assets,CockpitFrame frame){
    for(const std::uint8_t damage:{std::uint8_t(0),std::uint8_t(9)}) {
        frame.damageStrip.fill(damage);
        std::vector<std::uint32_t> original(320*200,0x234567),extended=original;
        composeReplayCockpit(assets,frame,original);composeReplayCockpit(assets,frame,extended,true);
        for(unsigned y=0;y<8;++y)for(unsigned x=32;x<288;++x){const unsigned at=y*320+x;
            require(original[at]==assets.palette[damage],"Original damage-strip composition changed");
            require(extended[at]==(assets.exteriorMask[at]?0x234567:original[at]),"Extended damage drawing covered the exterior or removed a roof detail");
        }
    }
}
unsigned assemblyVariants(const ReplayRenderAssets& assets,const NativeWheelSprites& sprites,const ReplaySceneFrame& source){
    unsigned cases=0;
    for(unsigned rotation=0;rotation<3;++rotation)for(unsigned boost=0;boost<4;++boost)
    for(unsigned pose=0;pose<3;++pose)for(unsigned hardware=0;hardware<12;++hardware){
        auto frame=source.cockpit;frame.wheelRotation=std::uint8_t(rotation);
        frame.boostActive=boost?0x80:0;frame.flameFrame=std::uint8_t(boost?boost-1:0);
        auto placements=source.wheelSprites;
        const auto displacement=[&](bool left){return !pose?0:(pose==1)==left?-8:6;};
        for(unsigned id=0;id<6;++id){auto& o=frame.objects[id];const int delta=displacement(o.x<160);
            o.y=std::uint16_t(int(o.y)+delta);o.height=std::uint16_t(std::clamp(int(o.height)-delta,1,int(assets.objects[id].sourceRows)));}
        for(auto& placement:placements.placements)placement.y+=displacement(placement.x<160);
        scr::WheelAnimationState animation;const unsigned phase=hardware%3,raised=hardware/3;
        animation.spritePointers={{sprites.animation.spritePointers[phase+((raised&1)?6:0)],sprites.animation.spritePointers[5-phase+((raised&2)?6:0)]}};
        std::vector<std::uint32_t> original,extended(320*200,0x234567);makeReplayBackground(assets,original);
        for(unsigned y=16;y<144;++y)for(unsigned x=32;x<288;++x)original[y*320+x]=0x234567;
        composeReplayFrame(assets,extended);
        composeReplayCockpit(assets,frame,original);composeReplayCockpit(assets,frame,extended,true);
        composeNativeWheelSprites(sprites,placements,animation,original);composeNativeWheelSprites(sprites,placements,animation,extended);
        compareArtwork(original,extended,assets,frame,sprites,placements,animation);
        ++cases;
    }
    return cases;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: native_extended_view_tests assets output");
    const std::filesystem::path pack=argv[1],output=argv[2];std::filesystem::create_directories(output);
    maskTests();ReplayViewSettings extended;extended.view=NativeView::Extended;
    unsigned queries=0,cornerChanges=0,variants=0,maskPixels=0;
    for(unsigned track=0;track<8;++track){
        auto seed=startNativeRace(pack,track);
        ReplayScene control,viewed;control.loadNativeState(*seed.memory,seed.payload,{});viewed.loadNativeState(*seed.memory,seed.payload,{});
        const auto assets=decodeReplayRenderAssets(seed.payload.data(),seed.payload.size(),seed.memory->data(),seed.memory->size());
        const auto sprites=decodeNativeWheelSprites(seed.memory->data(),seed.memory->size());
        scr::TrigTable trig{};for(unsigned i=0;i<trig.size();++i)trig[i]=std::uint16_t((unsigned((*seed.memory)[0x1ca42+2*i])<<8)|(*seed.memory)[0x1ca43+2*i]);
        if(track==0){roofMaskTests(assets,viewed.frames().back().cockpit);variants=assemblyVariants(assets,sprites,viewed.frames().back());maskPixels=unsigned(std::count(assets.exteriorMask.begin(),assets.exteriorMask.end(),1));}
        for(unsigned tick=0;tick<160&&!control.liveTerminal();++tick){
            const unsigned controls=1|(tick%40<8?16:0)|(tick>100&&tick%40<4?4:0);
            require(control.stepLive(controls)==viewed.stepLive(controls),"Extended changed race lifetime");
            if(tick%8==0){
                const auto original=control.renderLive(0,false,false);
                const auto image=viewed.renderLive(0,false,false,NativeView::Extended);
                const auto& exact=viewed.frames().back();compareArtwork(original,image,assets,exact.cockpit,sprites,exact.wheelSprites,exact.wheelTimeline[0]);
                for(unsigned at=0;at<assets.exteriorMask.size();++at)if(assets.exteriorMask[at]){
                    require(original[at]==assets.palette[0],"Exterior mask includes original foreground artwork");cornerChanges+=image[at]!=original[at];
                }
                require(viewed.renderLive(0,false,false,NativeView::Extended)==image,"Extended is not deterministic");
                require(viewed.render(1)==original,"Original view was not restored exactly");
                const auto midpoint=viewed.renderView(0.5,extended);require(viewed.renderView(0.5,extended)==midpoint,"Extended interpolation is not deterministic");
                const auto originalMidpoint=viewed.renderLive(0.5,false,true);
                const auto& from=viewed.frames().front();const auto& to=viewed.frames().back();
                const bool interpolated=canInterpolateReplay(from.restarts,to.restarts,from.action,to.action);
                const auto visual=interpolated?interpolateReplayVisuals(from,to,0.5,trig):static_cast<const ReplayVisualState&>(from);
                compareArtwork(originalMidpoint,midpoint,assets,visual.cockpit,sprites,from.wheelSprites,from.wheelTimeline[interpolated?3:0]);
                const auto clean=viewed.renderLive(0.5,false,true,NativeView::Extended);
                require(viewed.renderLiveMessage(0x4c,2).size()==320*200,"Extended pause panel changed dimensions");
                require(viewed.renderLive(0.5,false,true,NativeView::Extended)==clean,"Pause panel leaked into later frames");
                require(viewed.captureLiveIndices().size()==320*200&&viewed.captureLiveIndices(false,NativeView::Extended).size()==320*200,"Extended palette capture changed dimensions");
                require(viewed.renderLive(0.5,false,true,NativeView::Extended)==clean,"Palette capture changed Extended view");
                ++queries;
                if(tick==128){viewed.renderLive(0,false,false,NativeView::Extended);viewed.saveBitmap(output/("track-"+std::to_string(track)+"-extended.bmp"));
                    viewed.render(1);viewed.saveBitmap(output/("track-"+std::to_string(track)+"-original.bmp"));}
            }
            require(viewed.frames().back().pose.worldPosition==control.frames().back().pose.worldPosition,"Extended changed the simulation camera");
        }
        for(unsigned n=0;n<600&&!control.liveTerminal();++n){control.stepLive(0,true);viewed.stepLive(0,true);}
        require(control.liveTerminal()&&viewed.liveTerminal(),"Original quit gate did not finish");
        auto expected=std::make_unique<NativeMemory>(*seed.memory),actual=std::make_unique<NativeMemory>(*seed.memory);
        control.copyLiveExit(*expected);viewed.copyLiveExit(*actual);require(*expected==*actual,"Extended changed race/RNG/exit memory");
    }
    require(maskPixels==3166&&cornerChanges>0,"Extended changed the expected repaired exterior outline");
    const auto edge=scr::makeNearTrackEdge({-32,-16},{-1,-1},32,16);require(!(edge.clipFlags&15),"Extended upper/side clipping rejected visible world geometry");
    std::cout<<"{\"status\":\"passed\",\"tracks\":8,\"queries\":"<<queries<<",\"assembly_variants\":"<<variants<<",\"mask_pixels\":"<<maskPixels<<",\"corner_changes\":"<<cornerChanges<<",\"frame_seam_repairs\":2,\"original_car_artwork_exact\":true,\"original_bitmap_unchanged\":true,\"simulation_memory_identical\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

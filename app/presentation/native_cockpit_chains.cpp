#include "presentation/native_cockpit_chains.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::runtime {
NativeChainState captureNativeChainState(const NativeMemory& memory){
    return {memory[0x1bbea],memory[0x1bbe9]};
}
NativeChainDraws advanceNativeChains(std::uint8_t raceStartTimer,NativeChainState& state,
        const CockpitFrame& cockpit){
    NativeChainDraws draws;
    if(!raceStartTimer){
        // Disk 0x662c8 tests equality, unlike the enhanced source's BLS.
        if(state.position==0x60)return draws;
        state.position=std::uint8_t(state.position-state.velocity);
        state.velocity=std::uint8_t(state.velocity+8);
    }
    const auto append=[&](unsigned id,int y){
        auto placement=cockpit.objects[id];placement.y=std::uint8_t(y);
        draws.push_back({std::uint8_t(id),placement});
    };
    auto position=state.position;
    for(;;){
        const int upper=int(position)-0x30,lower=upper+8;
        if(lower<16)return draws;
        append(20,lower);append(22,lower);
        if(upper<16)return draws;
        append(19,upper);append(21,upper);
        position=std::uint8_t(position-16);
    }
}
void composeNativeChains(const RaceRenderAssets& assets,const NativeChainDraws& draws,
        std::vector<std::uint32_t>& pixels){
    if(pixels.size()!=raceCanvasWidth*raceCanvasHeight)throw std::invalid_argument("Expected320x200 chain canvas");
    for(const auto& draw:draws){
        if(draw.objectId<19||draw.objectId>22)throw std::invalid_argument("Chain object outside original19..22 range");
        compositeMaskedObject(assets,draw.objectId,draw.placement,pixels);
    }
}
void composeInterpolatedNativeChains(const RaceRenderAssets& assets,const NativeChainDraws& from,
        const NativeChainDraws& to,double fraction,std::vector<std::uint32_t>& pixels){
    if(!std::isfinite(fraction))throw std::invalid_argument("Chain fraction must be finite");
    if(fraction<=0){composeNativeChains(assets,from,pixels);return;}
    if(fraction>=1){composeNativeChains(assets,to,pixels);return;}
    if(pixels.size()!=320*200)throw std::invalid_argument("Expected320x200 chain canvas");
    if(from.empty())return; // A new hanging sequence appears only at its tick.
    int distance=0;
    if(!to.empty())distance=int(to.front().placement.y)-int(from.front().placement.y);
    else{
        int bottom=16;
        for(const auto& draw:from)bottom=std::max(bottom,int(draw.placement.y)+int(draw.placement.height));
        distance=16-bottom;
    }
    // A reset/lowering discontinuity is never a lift to interpolate through.
    if(distance>0||distance<-128){composeNativeChains(assets,from,pixels);return;}
    const int offset=int(std::lround(distance*fraction));
    if(!offset){composeNativeChains(assets,from,pixels);return;}
    std::vector<std::uint32_t> layer(320*200,transparentPixel);
    composeNativeChains(assets,from,layer);
    for(int y=16;y<200;++y){
        const int source=y-offset;if(source>=200)break;
        for(unsigned x=0;x<320;++x){const auto pixel=layer[unsigned(source)*320+x];
            if(pixel!=transparentPixel)pixels[unsigned(y)*320+x]=pixel;
        }
    }
}
} // namespace scr::runtime

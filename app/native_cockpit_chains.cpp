#include "native_cockpit_chains.h"
#include <stdexcept>

namespace scr::viewer {
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
void composeNativeChains(const ReplayRenderAssets& assets,const NativeChainDraws& draws,
        std::vector<std::uint32_t>& pixels){
    if(pixels.size()!=replayWidth*replayHeight)throw std::invalid_argument("Expected320x200 chain canvas");
    for(const auto& draw:draws){
        if(draw.objectId<19||draw.objectId>22)throw std::invalid_argument("Chain object outside original19..22 range");
        compositeMaskedObject(assets,draw.objectId,draw.placement,pixels);
    }
}
} // namespace scr::viewer

#include "track/draw_bridge.h"

namespace scr {
namespace {
std::uint8_t byte(unsigned value){return static_cast<std::uint8_t>(value);}
std::uint16_t word(unsigned value){return static_cast<std::uint16_t>(value);}
void opponentHeights(const DrawBridgeData& data,unsigned phase,DrawBridgeState& state){
    const auto offset=data.opponentHeightOffsets[(phase&31U)>>1];
    auto height=std::uint8_t(0xc6);
    for(auto& value:state.opponentHeights){height=byte(unsigned(height)+offset);value=height;}
}
} // namespace
DrawBridgeUpdate animateDrawBridge(const DrawBridgeData& data,
    const DrawBridgeInputs& inputs,DrawBridgeState& state){
    if(inputs.trackId!=5)return {};
    const auto onBridge=[](unsigned segment){return segment>=0x33&&segment<0x38;};
    if(onBridge(inputs.playerSegment)||onBridge(inputs.opponentSegment)||
        (inputs.opponentSegment<0x38&&inputs.opponentSegment>=0x30&&state.active)){
        state.active=0x0c;
        opponentHeights(data,byte(unsigned(state.animationPhase)+0x0c),state);
        return {};
    }
    if(!(inputs.frameThrottleFlag&0x80))state.animationPhase=byte(unsigned(state.animationPhase)+1);
    state.segmentProgressDistance=0;state.active=0;
    const unsigned phase=state.animationPhase&31U;
    // Original SUB.W #16 followed by NOT.W for negative values produces the
    // duplicated endpoints15..0,0..15, rather than an absolute-value triangle.
    const unsigned triangle=phase<16?15-phase:phase-16;
    state.rampAngles.fill(data.angleTable[triangle]);
    state.stepHeight=word((triangle+4U)<<5);state.rawTrackDataOffset=data.geometryOffset;
    unsigned index=2;
    for(unsigned step=0;step<15;++step){
        state.segmentProgressDistance=word(unsigned(state.segmentProgressDistance)+state.stepHeight);
        do{
            auto high=byte(state.segmentProgressDistance>>8);
            if(index==0x20)high|=0x80;
            state.geometry[index]=high;
            state.geometry[index+1]=byte(state.segmentProgressDistance);
            index+=2;state.scratchByte=byte(index);
            const unsigned mirror=0x48-index;
            state.geometry[mirror]=high;
            state.geometry[mirror+1]=byte(state.segmentProgressDistance);
        }while(index==0x12);
    }
    const bool cacheUpdated=state.updateFlag!=0;
    if(cacheUpdated){
        // The first cache value deliberately combines raw bytes0x10 and0x13.
        // This asymmetric pair is present in the original instructions.
        const auto first=word((((unsigned(state.geometry[0x10])<<8)|state.geometry[0x13])&0x7fffU)+data.firstSegmentBase);
        const auto second=word((((unsigned(state.geometry[0x20])<<8)|state.geometry[0x21])&0x7fffU)+data.secondSegmentBase);
        for(unsigned i=0;i<4;++i)state.cachedHeights[i]=first;
        for(unsigned i=4;i<8;++i)state.cachedHeights[i]=second;
    }
    state.updateFlag=0x80;
    if(inputs.opponentSegment==0x2f)opponentHeights(data,state.animationPhase,state);
    return {true,cacheUpdated};
}
} // namespace scr

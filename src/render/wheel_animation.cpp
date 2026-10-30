#include "render/wheel_animation.h"
#include <stdexcept>

namespace scr {
void advanceWheelRotationFrame(std::uint16_t velocity,WheelAnimationState& state) {
    if(velocity&0x8000) {
        state.phase=static_cast<std::uint8_t>(state.phase-1);
        if(state.phase&0x80)state.phase=2;
    } else {
        state.phase=static_cast<std::uint8_t>(state.phase+1);
        if(state.phase>=3)state.phase=0;
    }
}
WheelAnimationSelection updateWheelGraphics(const WheelAnimationData& data,
        const WheelAnimationInputs& inputs,WheelAnimationState& state) {
    if(state.phase>2)throw std::invalid_argument("Wheel animation phase outside original 0..2 range");
    const unsigned sum=unsigned(state.accumulator)+inputs.speedHigh;
    state.accumulator=static_cast<std::uint8_t>(sum);
    if(sum>255)advanceWheelRotationFrame(inputs.localForwardVelocity,state);
    WheelAnimationSelection selection{true,{{static_cast<std::uint8_t>(37+state.phase),
        static_cast<std::uint8_t>(42-state.phase)}}};
    for(unsigned wheel=0;wheel<2;++wheel) {
        const auto height=inputs.height[wheel];
        if((height&0x8000)||height<126)
            selection.spriteIds[wheel]=static_cast<std::uint8_t>(selection.spriteIds[wheel]+6);
        state.spritePointers[wheel]=data.spritePointers[selection.spriteIds[wheel]-37];
    }
    return selection;
}
WheelAnimationSelection updateWheelVblank(const WheelAnimationData& data,
        const WheelAnimationInputs& inputs,WheelAnimationState& state) {
    if(state.framesToWait)--state.framesToWait;
    if(state.framesToWaitWhenFading)--state.framesToWaitWhenFading;
    if(!(inputs.displayUpdate&0x80)||inputs.disabled)return {};
    return updateWheelGraphics(data,inputs,state);
}
} // namespace scr

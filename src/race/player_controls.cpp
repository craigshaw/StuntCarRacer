#include "race/player_controls.h"
#include <stdexcept>

namespace scr {
std::uint8_t adjustBoostFuel(std::uint8_t delta,std::uint8_t fuel,std::uint8_t maximum) noexcept {
    unsigned low=(delta&15u)+(fuel&15u);
    if(low>9) low+=6;
    unsigned decimalSum=(delta&0xf0u)+(fuel&0xf0u)+low;
    if(decimalSum>0x99) decimalSum+=0x60;
    const auto result=static_cast<std::uint8_t>(decimalSum);
    return result<maximum ? result : maximum;
}

PlayerControlsEffects processLocalPlayerInput(const PlayerControlsInputs& input,
                                             const ControlKeyMap& keys,PlayerControlsState& state) {
    for(auto key:keys) if(key>=input.keyboard.size()) throw std::out_of_range("Control key outside keyboard table");
    PlayerControlsEffects effects{};
    const auto random=static_cast<std::uint8_t>((state.randomSeed>>4)^((state.randomSeed>>16)>>1));
    state.randomSeed=(state.randomSeed<<8)|state.randomTail; state.randomTail=random;

    std::uint8_t pressed=0;
    for(unsigned key=0;key<5;++key) if(input.keyboard[keys[key]]==0xb3)
        pressed=static_cast<std::uint8_t>(pressed|(1u<<key));
    auto controls=pressed;
    if(pressed&0x10) controls|=1;
    if(pressed&2) controls|=0x10;
    if(pressed&1) controls=static_cast<std::uint8_t>((controls|2)&0xfe);
    if(!controls) {
        ++state.cipherIndex;
        effects.joystickRead=true; effects.ciaDataDirection=input.hardware.ciaDataDirection&0x7f;
        const auto joy=input.hardware.joy1dat;
        const auto decoded=static_cast<std::uint16_t>((joy>>1)^joy);
        if(decoded&0x100) controls|=1;
        if(decoded&1) controls|=2;
        if(joy&0x200) controls|=4;
        if(joy&2) controls|=8;
        if(!(input.hardware.ciaPortA&0x80)) controls|=0x10;
        state.joystickState=static_cast<std::uint8_t>(controls^0xff);
        if(!controls && !(input.displayUpdateFlag&0x80) && input.keyboard[0x44]==0xb3) controls=0x10;
    }
    state.inputFlags=controls&0x1f;

    // Original early branch retains the timer byte itself as steering output.
    auto steering=input.wheelMovementActive;
    if(steering) {
        steering=input.raceStartTimer;
        if(!steering) {
            steering=controls&0x0c;
            if(steering) steering=steering==4 ? 0xf1 : 0x0f;
        }
    }
    state.steeringDirection=steering;
    state.raceActive=static_cast<std::uint8_t>((controls&0x10)^0x10);
    std::uint8_t intensity=0,accumulator=0;
    const auto velocityHigh=static_cast<std::uint8_t>(input.localVelocityZ>>8);
    if((velocityHigh&0x80 || velocityHigh<0x78) && !input.raceStartTimer && !input.postWreckStateFlag) {
        const auto throttle=controls&3;
        if(throttle==1 || (!throttle && (state.accelerationState&0x80))) {
            intensity=input.gasInputIntensity; accumulator=input.gasInputAccumulator; state.accelerationState=0x80;
        } else if(throttle>1) {
            intensity=0x10; accumulator=0xff; state.accelerationState=0;
        }
    }
    state.gasOutput=static_cast<std::uint16_t>((unsigned(accumulator)<<8)|intensity);
    state.boostActive=0;
    if(!state.raceActive && !input.postWreckStateFlag
       && ((state.accelerationState&0x80) || (controls&3)) && state.boostFuel) {
        if(!(input.frameThrottleFlag&0x80)) {
            --state.fuelTimer;
            if(state.fuelTimer&0x80) {
                state.fuelTimer=input.fuelConsumptionRate;
                state.boostFuel=adjustBoostFuel(0x99,state.boostFuel,input.maxBoostFuel);
                effects.fuelHudRedraw=true; effects.displayedFuel=state.boostFuel;
            }
        }
        // The final fuel unit is consumed before this update's boost applies.
        state.boostActive=0x80;
        state.gasOutput=static_cast<std::uint16_t>(unsigned(state.gasOutput)*2);
    }
    return effects;
}
} // namespace scr

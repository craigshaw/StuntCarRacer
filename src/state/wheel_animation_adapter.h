#pragma once
#include "wheel_animation.h"
#include <cstddef>
#include <cstdint>

namespace scr::state::wheelAnimation {
inline std::uint16_t word(const std::uint8_t* r,unsigned a) {
    return static_cast<std::uint16_t>((unsigned(r[a])<<8)|r[a+1]);
}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a) {
    return (std::uint32_t(word(r,a))<<16)|word(r,a+2);
}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t value) {
    r[a]=static_cast<std::uint8_t>(value>>8);r[a+1]=static_cast<std::uint8_t>(value);
}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t value) {
    putWord(r,a,static_cast<std::uint16_t>(value>>16));putWord(r,a+2,static_cast<std::uint16_t>(value));
}
inline WheelAnimationData readData(const std::uint8_t* r) {
    WheelAnimationData data;
    for(unsigned i=0;i<12;++i)data.spritePointers[i]=longword(r,0x6a4ac+4*(37+i));
    return data;
}
inline WheelAnimationInputs readInputs(const std::uint8_t* r) {
    return {r[0x5d724],r[0x1bbb9],r[0x1bc62],word(r,0x1bd30),{{word(r,0x6a176),word(r,0x6a1a6)}}};
}
inline void writeInputs(std::uint8_t* r,const WheelAnimationInputs& in) {
    r[0x5d724]=in.displayUpdate;r[0x1bbb9]=in.disabled;r[0x1bc62]=in.speedHigh;
    putWord(r,0x1bd30,in.localForwardVelocity);putWord(r,0x6a176,in.height[0]);putWord(r,0x6a1a6,in.height[1]);
}
inline WheelAnimationState readState(const std::uint8_t* r) {
    return {r[0x1bbe3],r[0x1bbdb],{{(std::uint32_t(word(r,0xe7f0))<<16)|word(r,0xe7f4),
        (std::uint32_t(word(r,0xe7f8))<<16)|word(r,0xe7fc)}},r[0x616d8],r[0x616da]};
}
inline void writeState(std::uint8_t* r,const WheelAnimationState& state) {
    r[0x1bbe3]=state.accumulator;r[0x1bbdb]=state.phase;
    for(unsigned i=0;i<2;++i) {
        putWord(r,0xe7f0+8*i,static_cast<std::uint16_t>(state.spritePointers[i]>>16));
        putWord(r,0xe7f4+8*i,static_cast<std::uint16_t>(state.spritePointers[i]));
    }
    r[0x616d8]=state.framesToWait;r[0x616da]=state.framesToWaitWhenFading;
}
} // namespace scr::state::wheelAnimation

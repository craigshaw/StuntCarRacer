#pragma once
#include "wheel_animation.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scr::viewer {
struct CockpitFrame;
struct NativeWheelSpriteImage {
    std::uint32_t address{};
    std::vector<std::array<std::uint16_t,2>> rows;
};
struct NativeWheelSprites {
    WheelAnimationData animation;
    std::array<NativeWheelSpriteImage,12> images;
    // Hardware sprite palette values are already 4-bit OCS channel values,
    // unlike the 3-bit source palette used by the bitmap graphics.
    std::array<std::uint32_t,4> palette{};
};
struct NativeWheelSpritePlacement {
    int x{}, y{};
    unsigned rows{};
};
struct NativeWheelSpriteFrame {
    std::array<NativeWheelSpritePlacement,12> placements{};
};
// Translate immutable sprite images with the interpolated software suspension.
// Image selection, DMA spans and the independent 50 Hz rotation remain discrete.
NativeWheelSpriteFrame interpolateNativeWheelSprites(const NativeWheelSpriteFrame& from,
    const NativeWheelSpriteFrame& to,const CockpitFrame& fromCockpit,const CockpitFrame& toCockpit,double fraction);
// Decode initialized local assets, not the compressed original source sheet.
// Pixel 0 is transparent; the two hardware planes select colours 1..3.
[[nodiscard]] NativeWheelSprites decodeNativeWheelSprites(
    const std::uint8_t* chipRam,std::size_t chipBytes);
// Capture the live headers, applying the original pending position update
// logically when 0x69f41 is set. Simulation RAM is never changed. This preserves
// distinct normal and raised-wheel positions.
[[nodiscard]] NativeWheelSpriteFrame captureNativeWheelSpriteFrame(
    const NativeWheelSprites&,const std::uint8_t* chipRam,std::size_t chipBytes);
// Composite current sprite pointers after the software cockpit wheel images.
// The caller owns the independent 50 Hz wheel-animation timeline.
void composeNativeWheelSprites(const NativeWheelSprites&,
    const NativeWheelSpriteFrame&,const WheelAnimationState&,
    std::vector<std::uint32_t>& pixels);
} // namespace scr::viewer

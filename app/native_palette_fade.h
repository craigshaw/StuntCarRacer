#pragma once
#include "native_asset_initialization.h"
#include <array>
#include <cstdint>

namespace scr::viewer {
using NativePalette=std::array<std::uint16_t,16>;
using NativeRgbPalette=std::array<std::uint32_t,16>;
[[nodiscard]] NativePalette readNativePalette(const NativeMemory&,unsigned address);
// Native counterpart of 0x6444e / 0x64470 and the PAL vblank countdowns.
// Palette channels retain the source's 0..7 range until copper conversion.
class NativePaletteFade {
public:
    void reset(const NativePalette&);
    void fadeToColor(std::uint16_t colour);
    void animateTo(const NativePalette&);
    // A single presentation update may span at most sixty seconds. Native
    // callers ordinarily supply their bounded 60 Hz presentation delta.
    void advance(double seconds);
    void advanceVBlank(unsigned count=1);
    [[nodiscard]] bool active()const noexcept{return phase_!=Phase::Idle;}
    [[nodiscard]] const NativePalette& current()const noexcept{return current_;}
    [[nodiscard]] const NativePalette& target()const noexcept{return target_;}
    [[nodiscard]] const NativePalette& displayed()const noexcept{return displayed_;}
    [[nodiscard]] NativeRgbPalette rgbPalette()const noexcept;
    [[nodiscard]] unsigned stepCountdown()const noexcept{return stepCountdown_;}
    [[nodiscard]] unsigned minimumCountdown()const noexcept{return minimumCountdown_;}
private:
    enum class Phase {Idle,StepWait,MinimumWait};
    NativePalette current_{},target_{},displayed_{};
    Phase phase_=Phase::Idle;
    unsigned stepCountdown_{},minimumCountdown_{};
    double accumulator_{};
    void step();
};
} // namespace scr::viewer

#include "native_palette_fade.h"
#include "planar_image.h"
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
namespace {
void validate(std::uint16_t colour){
    if(colour&0xf888)throw std::invalid_argument("Original palette channel outside 0..7 range");
}
void validate(const NativePalette& palette){for(auto colour:palette)validate(colour);}
}
NativePalette readNativePalette(const NativeMemory& memory,unsigned address){
    if(address>memory.size()-32)throw std::invalid_argument("Native palette outside memory");
    NativePalette result{};
    for(unsigned i=0;i<16;++i)result[i]=std::uint16_t((unsigned(memory[address+i*2])<<8)|memory[address+i*2+1]);
    validate(result);return result;
}
void NativePaletteFade::reset(const NativePalette& palette){
    validate(palette);current_=target_=displayed_=palette;
    phase_=Phase::Idle;stepCountdown_=minimumCountdown_=0;accumulator_=0;
}
void NativePaletteFade::fadeToColor(std::uint16_t colour){
    validate(colour);if(active())throw std::logic_error("Original palette fade is still active");
    // Original writes current entry zero before filling the target palette.
    // The displayed copper words change only if step() finds a difference.
    current_[0]=colour;target_.fill(colour);minimumCountdown_=30;step();
}
void NativePaletteFade::animateTo(const NativePalette& palette){
    validate(palette);if(active())throw std::logic_error("Original palette fade is still active");
    target_=palette;step();
}
void NativePaletteFade::step(){
    bool changed=false;
    for(unsigned i=0;i<16;++i) {
        unsigned colour=0;
        for(unsigned shift:{0u,4u,8u}) {
            unsigned channel=(current_[i]>>shift)&15;
            const unsigned target=(target_[i]>>shift)&15;
            if(channel<target){++channel;changed=true;}
            else if(channel>target){--channel;changed=true;}
            colour|=channel<<shift;
        }
        current_[i]=static_cast<std::uint16_t>(colour);
    }
    if(changed){displayed_=current_;stepCountdown_=2;phase_=Phase::StepWait;}
    else {stepCountdown_=0;phase_=minimumCountdown_?Phase::MinimumWait:Phase::Idle;}
}
void NativePaletteFade::advanceVBlank(unsigned count){
    // Counters run in the interrupt even while the calling fade routine waits.
    for(unsigned frame=0;frame<count;++frame) {
        if(stepCountdown_)--stepCountdown_;
        if(minimumCountdown_)--minimumCountdown_;
        if(phase_==Phase::StepWait&&!stepCountdown_)step();
        else if(phase_==Phase::MinimumWait&&!minimumCountdown_)phase_=Phase::Idle;
    }
}
void NativePaletteFade::advance(double seconds){
    if(!std::isfinite(seconds)||seconds<0||seconds>60)
        throw std::invalid_argument("Palette elapsed time must be finite and within 0..60 seconds");
    accumulator_+=seconds;
    const auto frames=static_cast<unsigned>(std::floor((accumulator_+1e-12)*50));
    accumulator_=std::fmax(0.0,accumulator_-double(frames)/50);
    advanceVBlank(frames);
}
NativeRgbPalette NativePaletteFade::rgbPalette()const noexcept{
    NativeRgbPalette result{};for(unsigned i=0;i<16;++i)result[i]=originalPaletteRgb(displayed_[i]);return result;
}
} // namespace scr::viewer

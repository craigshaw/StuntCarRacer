#pragma once
#include "replay_interpolation.h"
#include <cstdint>

namespace scr::viewer {
enum class NativeView { Original, Extended, Widescreen };
// Original artwork retains its scale, centred on the wider canvas.
constexpr int extendedHorizontalPadding=32,extendedTopPadding=16;
constexpr int viewWidth(NativeView view){return view==NativeView::Widescreen?replayWidescreenWidth:320;}
constexpr int viewPadding(NativeView view){return view==NativeView::Widescreen?88:view==NativeView::Extended?32:0;}
// Presentation angles use the original wrapping 16-bit turn. These offsets
// apply to a private render pose, never to the driver's simulation camera.
struct ReplayViewSettings {
    std::int16_t yawOffset{},pitchOffset{},rollOffset{};
    bool showCockpit{true};
    NativeView view{NativeView::Original};
    bool hasCameraOffset()const noexcept{return yawOffset||pitchOffset||rollOffset;}
};
ReplayVisualState makeReplayView(const ReplayVisualState&,const ReplayViewSettings&,const TrigTable&);

struct PresentationRectangle {int x{},y{},width{},height{};};
// Fit any source image into a host surface without changing its aspect ratio.
// The renderer owns source dimensions; window shape does not affect gameplay.
PresentationRectangle fitPresentationImage(int sourceWidth,int sourceHeight,
    int targetWidth,int targetHeight,bool integerScale=true);
} // namespace scr::viewer

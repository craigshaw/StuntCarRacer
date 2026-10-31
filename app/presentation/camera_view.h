#pragma once
#include "presentation/presentation_interpolation.h"
#include <array>
#include <cstdint>

namespace scr::runtime {
enum class NativeView { Original, Extended, Widescreen, Ultrawide };
// Original artwork retains its scale, centred on the wider canvas.
constexpr int extendedHorizontalPadding=32,extendedTopPadding=16;
constexpr bool isWideView(NativeView view){return view==NativeView::Widescreen||view==NativeView::Ultrawide;}
constexpr int viewWidth(NativeView view){return view==NativeView::Ultrawide?ultrawideCanvasWidth:view==NativeView::Widescreen?widescreenCanvasWidth:320;}
constexpr int viewPadding(NativeView view){return isWideView(view)?(viewWidth(view)-256)/2:view==NativeView::Extended?32:0;}
// Presentation angles use the original wrapping 16-bit turn. These offsets
// apply to a private render pose, never to the driver's simulation camera.
struct CameraViewSettings {
    std::int16_t yawOffset{},pitchOffset{},rollOffset{};
    bool showCockpit{true};
    NativeView view{NativeView::Original};
    // Developer observer displacement in mesh/world units. No player setting
    // selects this; the default view and original cockpit scale are unchanged.
    std::array<double,3> worldOffset{};
    bool hasCameraOffset()const noexcept{return yawOffset||pitchOffset||rollOffset||worldOffset[0]||worldOffset[1]||worldOffset[2];}
};
PresentationPose makeCameraView(const PresentationPose&,const CameraViewSettings&,const TrigTable&);

struct PresentationRectangle {int x{},y{},width{},height{};};
// Fit any source image into a host surface without changing its aspect ratio.
// The renderer owns source dimensions; window shape does not affect gameplay.
PresentationRectangle fitPresentationImage(int sourceWidth,int sourceHeight,
    int targetWidth,int targetHeight,bool integerScale=true);
} // namespace scr::runtime

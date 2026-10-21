#pragma once
#include "native_asset_initialization.h"
#include "replay_render_assets.h"

namespace scr::viewer {
// Discrete message inputs from the completed gameplay frame. Retain them with
// the pose so interpolation and historical views never consult a later tick.
struct NativeRaceMessageFrame {
    std::uint8_t active{},countdown{},pause{},blink{},index{},mode{};
    ReplayObjectPlacement panel{};
    [[nodiscard]] bool visible()const noexcept;
};
[[nodiscard]] NativeRaceMessageFrame captureNativeRaceMessage(const NativeMemory&);
// Normal race frames draw only the original transparent glyphs. Pause/end
// screens restore masked object 51 first, matching their separate caller.
// immutableAssets supplies only the initialized font and message table.
void drawNativeRaceMessage(const NativeMemory& immutableAssets,const ReplayRenderAssets&,
    const NativeRaceMessageFrame&,std::vector<std::uint32_t>& pixels,bool restorePanel=false);
} // namespace scr::viewer

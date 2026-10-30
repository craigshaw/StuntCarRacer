#pragma once
#include "assets/native_startup.h"
#include "presentation/race_render_assets.h"

namespace scr::runtime {
struct NativeChainState {std::uint8_t position{},velocity{};};
struct NativeChainDraw {std::uint8_t objectId{};SpritePlacement placement{};};
using NativeChainDraws=std::vector<NativeChainDraw>;
[[nodiscard]] NativeChainState captureNativeChainState(const NativeMemory&);
// Renderer-owned animation: advance once per original rendered frame, including
// restart warm-ups. Retain ordered draws with that frame for immutable views.
[[nodiscard]] NativeChainDraws advanceNativeChains(std::uint8_t raceStartTimer,
    NativeChainState&,const CockpitFrame&);
void composeNativeChains(const RaceRenderAssets&,const NativeChainDraws&,
    std::vector<std::uint32_t>& pixels);
// Scroll the earlier original links towards the next snapshot, clipping their
// upper exit at y16. A final lift retracts the remaining links before removal.
void composeInterpolatedNativeChains(const RaceRenderAssets&,const NativeChainDraws& from,
    const NativeChainDraws& to,double fraction,std::vector<std::uint32_t>& pixels);
} // namespace scr::runtime

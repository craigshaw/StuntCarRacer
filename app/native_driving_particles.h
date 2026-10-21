#pragma once
#include "native_startup.h"
#include "replay_render_assets.h"
#include "driving_effects.h"

namespace scr::viewer {
struct NativeDrivingParticleData {
    std::array<std::uint16_t,8> offsets{};
    std::array<std::uint8_t,16> animation{};
};
NativeDrivingParticleData decodeNativeDrivingParticleData(const NativeMemory&);
// Read-only presentation of the original dust objects and 2x2 edge sparks.
void composeNativeDrivingParticles(const ReplayRenderAssets&,const NativeDrivingParticleData&,
    const DrivingParticleDraws&,std::uint8_t frameCounter,std::vector<std::uint32_t>& pixels);
} // namespace scr::viewer

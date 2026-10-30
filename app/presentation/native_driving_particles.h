#pragma once
#include "assets/native_startup.h"
#include "presentation/race_render_assets.h"
#include "race/driving_effects.h"

namespace scr::runtime {
struct NativeDrivingParticleData {
    std::array<std::uint16_t,8> offsets{};
    std::array<std::uint8_t,16> animation{};
};
NativeDrivingParticleData decodeNativeDrivingParticleData(const NativeMemory&);
// Move only continuing lifetimes. Births, retired draws, effect changes and the
// original sprite phase stay at the earlier tick until the exact endpoint.
DrivingParticleDraws interpolateNativeDrivingParticles(const DrivingParticleDraws& from,
    const DrivingParticleDraws& to,double fraction);
// Read-only presentation of the original dust objects and 2x2 edge sparks.
void composeNativeDrivingParticles(const RaceRenderAssets&,const NativeDrivingParticleData&,
    const DrivingParticleDraws&,std::uint8_t frameCounter,std::vector<std::uint32_t>& pixels);
} // namespace scr::runtime

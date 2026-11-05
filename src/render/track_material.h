#pragma once
#include "render/render_projection.h"
#include <array>
#include <cstdint>

namespace scr {
struct TrackTextureCoordinate { double u{},v{}; };
// Private presentation attributes. U runs across the road; V follows the
// authored segment/section order, so moving bridge panels keep their mapping.
struct TrackTexturePatch {
    std::array<RenderCameraRelative,4> vertices{};
    std::array<TrackTextureCoordinate,4> coordinates{};
};
// Original, deterministic procedural artwork, sampled from a mipmapped bitmap
// atlas. Derivatives are in UV units per screen pixel; no gameplay RNG is used.
std::uint32_t sampleTrackMaterial(bool road,std::uint32_t originalColor,
    TrackTextureCoordinate uv,TrackTextureCoordinate dx,TrackTextureCoordinate dy);
} // namespace scr

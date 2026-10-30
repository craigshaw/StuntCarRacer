#pragma once
#include "presentation/race_render_assets.h"
#include "render/render_projection.h"

namespace scr::runtime {
constexpr double gameplayTickSeconds=0.12;
constexpr unsigned presentationHz=60;
struct PresentationPose {
    RenderCameraInputs pose{};
    RenderProjectionCamera camera{};
    CockpitFrame cockpit{};
    std::uint16_t forwardSpeed{};
};
// Presentation only. Angles follow the shortest wrapped arc; discrete sprite
// frames, damage and gameplay events belong to the earlier verified tick.
// Exact endpoints are returned unchanged, including the original camera roll.
PresentationPose interpolatePresentation(const PresentationPose& from,
    const PresentationPose& to,double fraction,const TrigTable&);
bool canInterpolatePresentation(unsigned fromRestarts,unsigned toRestarts,
    unsigned fromAction,unsigned toAction) noexcept;
} // namespace scr::runtime

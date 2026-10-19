#pragma once
#include "replay_render_assets.h"
#include "render_projection.h"

namespace scr::viewer {
constexpr double replayTickSeconds=0.12;
constexpr unsigned replayPresentationHz=60;
struct ReplayVisualState {
    RenderCameraInputs pose{};
    RenderProjectionCamera camera{};
    CockpitFrame cockpit{};
    std::uint16_t forwardSpeed{};
};
// Presentation only. Angles follow the shortest wrapped arc; discrete sprite
// frames, damage and gameplay events belong to the earlier verified tick.
// Exact endpoints are returned unchanged, including the original camera roll.
ReplayVisualState interpolateReplayVisuals(const ReplayVisualState& from,
    const ReplayVisualState& to,double fraction,const TrigTable&);
bool canInterpolateReplay(unsigned fromRestarts,unsigned toRestarts,
    unsigned fromAction,unsigned toAction) noexcept;
} // namespace scr::viewer

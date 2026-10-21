#pragma once
#include "opponent_car.h"
#include "opponent_far_track.h"
#include <optional>

namespace scr::viewer {
struct ReplaySceneCommands {
    NearTrackPresentation presentation;
    NearTrackGeometryState geometry;
    std::uint16_t denseLineBytes{},farLineBytes{},backgroundLineBytes{},lineBytes{};
    std::uint16_t remainingOpponentOffset{0xffff};
    std::uint8_t horizonMode{};
    bool opponentAttempted{},opponentAdmitted{};
};

// The dense frame and geometry are the output of buildNearTrackFrame. The
// returned history belongs to presentation; no input or gameplay RAM is changed.
ReplaySceneCommands buildReplaySceneCommands(const NearTrackFrame&,
    const NearTrackDrawInputs&,const RenderProjectionTables&,
    const OpponentFarTrackData&,const NearTrackGeometryState&,
    const MountainHorizonData&,std::uint16_t yaw,const RenderProjectionCamera&,
    const std::optional<OpponentCarInputs>& opponent=std::nullopt);
} // namespace scr::viewer

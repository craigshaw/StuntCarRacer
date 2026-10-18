#ifndef SCR_ROAD_SUSPENSION_STEP_H
#define SCR_ROAD_SUSPENSION_STEP_H

#include "expected_wheel_heights.h"
#include "suspension_response.h"
#include "track_sampling.h"
#include "transform_matrices.h"

namespace scr {

struct RoadSuspensionInputs {
    TransformAngles angles;
    std::uint32_t worldHeight;
    // Wheel positions/pitch below are supplied by this step. Other fields,
    // including speed and segment position, come from the surrounding game.
    TrackSamplingInputs track;
    // Heights/pitch below are supplied by this step. Velocity integration,
    // view-angle calculation and the race state still provide other inputs.
    SuspensionResponseInputs response;
};

struct RoadSuspensionState {
    TransformMatrices matrices;
    WheelCoordinates wheelPositions;
    TrackSamplingState track;
    std::array<WheelTrackSample,3> samples;
    ExpectedWheelHeights expectedHeights;
    SuspensionResponseState response;
};

// Compose the recovered road/contact/response path with ordinary native calls.
// This is not the complete original physics tick: motion/view-angle stages,
// countdown, opponent collisions, audio and integration remain external.
// The track sampler's valid asset/index contract applies; it may throw.
void updateRoadSuspensionStep(const RoadSuspensionInputs& input,
                              const TrackGeometryData& geometry,
                              const TrigTable& trig,
                              const SuspensionAttenuationTable& attenuation,
                              RoadSuspensionState& state);

} // namespace scr
#endif

#include "physics_prefix.h"

namespace scr {

void updatePhysicsPrefix(const PhysicsPrefixInputs& inputs,
                         const TrackGeometryData& geometry,
                         const TrigTable& trig,
                         const MotionTransformSelectors& selectors,
                         const SuspensionAttenuationTable& attenuation,
                         PhysicsPrefixState& state) {
    auto& road = state.road;
    updateTransformMatrices(inputs.road.angles, trig, road.matrices);
    road.wheelPositions = calculateWheelCoordinates(road.matrices.wheelInputs());

    auto trackInputs = inputs.road.track;
    trackInputs.wheels = road.wheelPositions;
    trackInputs.pitchHighByte = static_cast<std::uint8_t>(inputs.road.angles.pitch >> 8);
    // The speed magnitude is refreshed later, after road sampling this tick.
    trackInputs.speedHighByte = static_cast<std::uint8_t>(state.wheelSpeed.carSpeedMagnitude >> 8);
    road.samples = sampleTrackWheelHeights(geometry, trackInputs, road.track);
    road.expectedHeights = calculateExpectedWheelHeights(
        inputs.road.angles.pitch, inputs.road.angles.roll, inputs.road.worldHeight, trig);

    transformWorldCoordinates(inputs.motion, road.matrices, selectors, state.motion);
    updateWheelSpeed({state.motion.localVelocity[2], road.response.wheelMovementActive}, state.wheelSpeed);
    calculateViewAngles(road.matrices, state.motion);

    auto responseInputs = inputs.road.response;
    responseInputs.wheelHeight = road.track.heights;
    responseInputs.expectedHeight = {
        static_cast<std::uint32_t>(road.expectedHeights.frontLeft),
        static_cast<std::uint32_t>(road.expectedHeights.frontRight),
        static_cast<std::uint32_t>(road.expectedHeights.rearCenter)};
    responseInputs.pitchAngle = inputs.road.angles.pitch;
    updateSuspensionResponse(responseInputs, attenuation, road.response);
}

} // namespace scr

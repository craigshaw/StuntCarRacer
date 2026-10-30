#include "physics/physics_update.h"

namespace scr {
std::vector<AudioRegisterWrite> updateGamePhysics(
    const PhysicsInputs& in, const PhysicsData& data, PhysicsState& state) {
    auto& road = state.prefix.road;
    auto& response = road.response;
    auto& transforms = state.prefix.motion;
    auto& motion = state.motion;
    auto& effects = state.effects;

    PhysicsPrefixInputs beginning{};
    beginning.road.angles = {motion.angles[0], motion.angles[1], motion.angles[2], in.trackHeading};
    beginning.road.worldHeight = motion.worldPosition[1];
    beginning.road.track.lateralOrigin = in.lateralOrigin;
    beginning.road.track.forwardOrigin = in.forwardOrigin;
    beginning.road.track.playerSegment = in.playerSegment;
    beginning.road.track.clampByteAtFF = in.clampByteAtFF;
    auto& suspension = beginning.road.response;
    suspension.trackOffsetAdjustment = in.trackOffsetAdjustment;
    suspension.trackWidthMultiplier = in.trackWidthMultiplier;
    suspension.frameThrottleFlag = in.frameThrottleFlag;
    suspension.damageFrameLimit = in.damageFrameLimit;
    suspension.raceStartTimer = effects.race.timer;
    suspension.trackId = in.trackId;
    suspension.rotationSpeedX = motion.rotationVelocity[0];
    beginning.motion.worldSpeed = motion.worldVelocity;
    updatePhysicsPrefix(beginning, data.track, data.trig, data.selectors, data.attenuation, state.prefix);

    effects.springAngles = {response.springs.rollAngle, response.springs.pitchAngle, response.springs.yawAngle};
    effects.race.carRollAngle = motion.angles[2];
    effects.race.targetPitchRate = response.targetPitchRate;
    SuspensionEffectsInputs effectInputs{in.carHeight,
        in.trackSurfaceHeight, in.lateralOffset, in.frameThrottleFlag, in.networkMode,
        in.multiplayerSync, in.playerSegment, in.opponentSegment, in.selectedTrack, in.raceActive,
        response.hardImpactCount, response.maxCompressionVelocity};
    const auto requests = runSuspensionEffects(effectInputs, effects);
    response.springs.rollAngle = effects.springAngles[0];
    response.springs.pitchAngle = effects.springAngles[1];
    response.springs.yawAngle = effects.springAngles[2];
    response.springs.retainedWord46 = effects.savedYawSpring;
    response.targetPitchRate = effects.race.targetPitchRate;
    motion.angles[2] = effects.race.carRollAngle;

    auto audioData = data.audio;
    audioData.samples[3].volume = static_cast<std::uint16_t>((audioData.samples[3].volume & 0xff00) | effects.impactVolume);
    std::vector<AudioRegisterWrite> audioWrites;
    for (unsigned i = 0; i < requests.count; ++i) {
        const auto writes = requestSample(requests.ids[i], audioData, state.audio);
        audioWrites.insert(audioWrites.end(), writes.begin(), writes.end());
    }

    // These fields share addresses with the preceding stages in the original.
    // Refresh them even on the skipped path so retained native state is coherent.
    state.steering.segment = road.track.segment;
    state.steering.currentSegment = road.track.currentSegment;
    state.steering.yawAngle = motion.angles[1];
    state.steering.scratch3 = response.springs.scratch.multiplier;
    state.steering.scratch4 = response.springs.scratch.productLow;
    state.steering.angleDeltaScratch = response.frontAverageVelocity;

    if (in.frameProcessingFlag) {
        updateCarOrientation({transforms.baseRollAngle, effects.springAngles[1], effects.springAngles[2],
            transforms.additionalYawOffset, transforms.baseTargetRoll, effects.springAngles[0],
            transforms.localVelocity[0], transforms.localVelocity[2], response.wheelMovementActive}, state.orientation);
        calculateSteeringResponse(data.track, data.steeringOffsets,
            {in.trackHeading, transforms.localVelocity[2], motion.rotationVelocity[1], in.playerSegment,
             in.trackProgression, response.wheelMovementActive, in.reverseDirectionLowByte, in.integrityWord}, state.steering);
        motion.angles[1] = state.steering.yawAngle;

        MotionTransformInputs transformInputs{};
        transformInputs.adjustedAngles = {state.orientation.roll.adjustedRollAngle,
            state.orientation.adjustedPitchAngle, state.orientation.adjustedYawAngle};
        calculateSecondaryCoordinates(transformInputs, road.matrices, data.selectors, transforms);
        CarDragState drag{transforms.worldAcceleration};
        updateCollisionDrag({transforms.localVelocity, motion.worldVelocity, response.wheelMovementActive,
            static_cast<std::uint8_t>(effects.savedYawSpring >> 8), effects.race.offTrackFlags,
            static_cast<std::uint8_t>(in.trackOffsetAdjustment >> 8), effects.race.timer,
            in.draftingTimer, in.opponentAhead}, drag);

        MotionTailState tail{motion, state.damping, transforms.transformedAngularVelocity};
        updateMotionTail({drag.worldAcceleration, state.steering.yawAcceleration, response.targetPitchRate,
            response.targetRollRate, state.orientation.adjustedYawAngle, response.wheelMovementActive,
            in.raceCompletionFlag, road.track.renderingOrderMode}, road.matrices, data.selectors, data.angleLimits, tail);
        motion = tail.integration;
        state.damping = tail.damping;
        transforms.worldAcceleration = drag.worldAcceleration;
        transforms.transformedAngularVelocity = tail.transformedAngularVelocity;
    } else {
        integrateWorldVelocity(transforms.worldAcceleration, motion);
        updateWorldPosition({transforms.transformedAngularVelocity, in.raceCompletionFlag, road.track.renderingOrderMode}, data.angleLimits, motion);
    }

    road.track.segment = state.steering.segment;
    road.track.currentSegment = state.steering.currentSegment;
    response.springs.scratch.multiplier = state.steering.scratch3;
    response.springs.scratch.productLow = state.steering.scratch4;
    response.frontAverageVelocity = state.steering.angleDeltaScratch;
    // Sampled-height scratch overlaps the two byte-multiply scratch bytes.
    road.track.sampledHeight = (road.track.sampledHeight & 0xffff0000u)
        | (unsigned(response.springs.scratch.multiplier) << 8) | response.springs.scratch.productLow;
    effects.race.carRollAngle = motion.angles[2];
    state.steering.yawAngle = motion.angles[1];
    return audioWrites;
}
} // namespace scr

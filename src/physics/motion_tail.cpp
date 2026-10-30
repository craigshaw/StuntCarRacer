#include "physics/motion_tail.h"

namespace scr {

void updateMotionTail(const MotionTailInputs& input, const TransformMatrices& matrices,
                      const MotionTransformSelectors& selectors, const MotionAngleLimits& limits,
                      MotionTailState& state) {
    auto& integrated = state.integration;
    updateVelocityDamping({integrated.rotationVelocity[0], integrated.rotationVelocity[2],
        input.targetPitchRate, input.targetRollRate, input.adjustedYawAngle, input.wheelMovementActive},
        state.damping);
    integrateAngularVelocity({state.damping.angularAccelerationX, input.angularAccelerationY,
                              state.damping.angularAccelerationZ}, integrated);

    MotionTransformInputs transformInput{};
    transformInput.rotationSpeed = integrated.rotationVelocity;
    MotionTransformState transformed{};
    calculateTertiaryCoordinates(transformInput, matrices, selectors, transformed);
    state.transformedAngularVelocity = transformed.transformedAngularVelocity;

    integrateWorldVelocity(input.worldAcceleration, integrated);
    updateWorldPosition({state.transformedAngularVelocity, input.raceCompletionFlag,
                         input.renderingOrderMode}, limits, integrated);
    // Position integration can cancel outward X/Z rotation at an angle limit;
    // the transformed angular words retain their values from before that clamp.
}

} // namespace scr

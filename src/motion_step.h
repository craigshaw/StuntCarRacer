#pragma once
#include "motion_integration.h"
#include "motion_transforms.h"
#include "wheel_motion.h"

namespace scr {

// Results supplied by earlier physics, including the still-unported orientation,
// steering and collision stages. Acceleration Y is retained by the damping stage.
struct MotionTailInputs {
    MotionVector worldAcceleration{};
    std::uint16_t angularAccelerationY{};
    std::uint16_t targetPitchRate{}, targetRollRate{}, adjustedYawAngle{};
    std::uint8_t wheelMovementActive{}, raceCompletionFlag{}, renderingOrderMode{};
};
struct MotionTailState {
    MotionIntegrationState integration{};
    VelocityDampingState damping{};
    MotionVector transformedAngularVelocity{};
};

// Original driver [0x618ae,0x618ce): damping, angular integration, tertiary
// transform, world-velocity integration, and position/angle integration.
// This is a continuous tail, not a complete physics tick. Coefficient selectors
// obey calculateTertiaryCoordinates' validity contract; limits are supplied data.
void updateMotionTail(const MotionTailInputs& inputs, const TransformMatrices& matrices,
                      const MotionTransformSelectors& selectors, const MotionAngleLimits& limits,
                      MotionTailState& state);

} // namespace scr

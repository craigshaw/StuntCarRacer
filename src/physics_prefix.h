#ifndef SCR_PHYSICS_PREFIX_H
#define SCR_PHYSICS_PREFIX_H

#include "motion_transforms.h"
#include "road_suspension_step.h"
#include "wheel_motion.h"

namespace scr {

struct PhysicsPrefixInputs {
    RoadSuspensionInputs road;
    MotionTransformInputs motion;
};

struct PhysicsPrefixState {
    RoadSuspensionState road;
    MotionTransformState motion;
    WheelSpeedState wheelSpeed;
};

// The first eight calls of the original physics driver, stopping inside the
// suspension routine immediately before its countdown call at 0x62042.
// Sampling uses the previous speed and wheel-speed calculation uses the
// previous contact flag, exactly as in the original call order.
// The track sampler and transform selector validity contracts apply.
void updatePhysicsPrefix(const PhysicsPrefixInputs& inputs,
                         const TrackGeometryData& geometry,
                         const TrigTable& trig,
                         const MotionTransformSelectors& selectors,
                         const SuspensionAttenuationTable& attenuation,
                         PhysicsPrefixState& state);

} // namespace scr
#endif

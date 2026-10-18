#pragma once
#include "audio_playback.h"
#include "car_dynamics.h"
#include "motion_step.h"
#include "physics_prefix.h"
#include "steering_response.h"
#include "suspension_effects.h"
#include <vector>

namespace scr {
// Inputs maintained outside updateGamePhysics: track progress, player controls,
// frame scheduling, race/network mode and the original runtime integrity word.
struct PhysicsInputs {
    std::uint16_t trackHeading{}, lateralOrigin{}, forwardOrigin{};
    std::uint16_t carHeight{}, trackSurfaceHeight{}, lateralOffset{};
    std::uint32_t trackOffsetAdjustment{}, integrityWord{};
    std::uint8_t playerSegment{}, opponentSegment{}, clampByteAtFF{}, trackProgression{};
    std::uint8_t reverseDirectionLowByte{}, trackWidthMultiplier{}, damageFrameLimit{}, trackId{};
    std::uint8_t frameProcessingFlag{}, frameThrottleFlag{}, networkMode{}, multiplayerSync{};
    std::uint8_t selectedTrack{}, raceActive{}, raceCompletionFlag{}, draftingTimer{}, opponentAhead{};
};
struct PhysicsData {
    TrackGeometryData track;
    TrigTable trig;
    MotionTransformSelectors selectors;
    SuspensionAttenuationTable attenuation;
    SteeringOffsetTable steeringOffsets;
    MotionAngleLimits angleLimits;
    AudioSampleData audio;
};
struct PhysicsState {
    PhysicsPrefixState prefix;
    SuspensionEffectsState effects;
    CarOrientationState orientation;
    SteeringResponseState steering;
    MotionIntegrationState motion;
    VelocityDampingState damping;
    AudioPlaybackState audio;
};

// Complete original driver [0x6185c,0x618ce), including both scheduling paths.
// Native typed game state and explicit audio bus effects; no CPU or RAM image.
// Existing valid track/selector/channel domains apply. This synchronous update
// does not reproduce interrupt timing, audio waveforms or the outer game loop.
[[nodiscard]] std::vector<AudioRegisterWrite> updateGamePhysics(
    const PhysicsInputs& inputs, const PhysicsData& data, PhysicsState& state);
} // namespace scr

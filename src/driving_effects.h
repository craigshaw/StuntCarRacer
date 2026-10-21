#pragma once
#include "transform_matrices.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
struct DamageSpriteWord {
    std::uint16_t preserveMask{};
    std::array<std::uint16_t,4> planes{};
};
// Each original hole variant is 32 pixels wide and 8 rows high. Words retain
// the Amiga mask/plane encoding as asset data, without exposing RAM pointers.
using DamageHoleSprite=std::array<DamageSpriteWord,16>;
struct DrivingEffectsInputs {
    std::uint16_t localForwardSpeed{}, speedMagnitude{}, enginePitchAccumulator{}, maxCompression{};
    std::array<std::uint16_t,2> wheelTravel{};
    std::uint8_t wheelMovementActive{}, inputFlags{}, offTrackFlags{}, networkMode{}, raceStartTimer{};
    std::array<DamageHoleSprite,4> holeSprites{}; // existing variants, new variants
};
struct DebrisParticle { std::uint16_t x{},y{},velocityX{},velocityY{}; };
// Original draw calls occur before movement and again for newly spawned
// particles. Keep their ordered coordinates separate from final gameplay state.
struct DrivingParticleDraw {std::uint16_t x{},y{};std::uint8_t index{};bool dust{};};
using DrivingParticleDraws=std::vector<DrivingParticleDraw>;
struct DrivingEffectsState {
    std::uint32_t randomSeed{};
    std::uint8_t randomTail{}, engineNoiseFlag{};
    std::uint16_t enginePitchDelta{}, edgeSamplePeriod{}, groundSamplePeriod{};
    std::uint8_t grindSample{}, impactLevel{}, particleLimit{}, particleScratch{};
    std::array<DebrisParticle,32> debris{};
    std::uint8_t bouncePhase{}, wheelBaseHeight{}, spriteUpdatePending{};
    std::array<std::uint16_t,2> wheelVisualHeight{};
    std::array<std::uint8_t,3> accumulatedForce{};
    std::uint8_t damageActive{}, targetDamage{}, carDamage{}, damageVerticalOffset{}, damageColor{};
    std::uint8_t majorImpactCooldown{}, holePosition{}, creakVolume{};
    std::uint8_t blinkCountdown{}, gameModeFlags{}, postWreck{}, visualEffectHighByte{};
    std::uint8_t messageIndex{}, messageMode{}, messageActive{};
    // Feedback strip: physical framebuffer pixels x32..287, y0..7. Occupancy
    // must agree with damagePixelColors. The game reads these pixels while
    // advancing damage, so both buffers' colors are simulation state.
    std::array<bool,256*8> damagePixels{};
    // Stores performed by the latest feedback update, including writes that
    // retain occupancy in the current buffer but change the other buffer.
    std::array<bool,256*8> damagePixelTouched{};
    std::array<std::uint8_t,256*8> damagePixelColors{};
    std::array<std::uint8_t,256*8> damageOtherPixelColors{};
};
struct DrivingVisualEvents {
    bool damageBarChanged{}, redrawExistingHole{}, drawNewHole{};
};
void updateEnginePitch(const DrivingEffectsInputs&,DrivingEffectsState&);
[[nodiscard]] std::vector<std::uint8_t> updateGroundAndEdgeEffects(
    const DrivingEffectsInputs&,DrivingEffectsState&,DrivingParticleDraws* draws=nullptr);
void updateWheelVisuals(const DrivingEffectsInputs&,const TrigTable&,DrivingEffectsState&);
// Captured damage UI domain: holePosition0..10, vertical offset2..6, and the
// four original32x8 hole assets. Pixels outside the feedback strip remain
// represented by drawNewHole/redrawExistingHole events for a future renderer.
[[nodiscard]] std::vector<std::uint8_t> updateDamageFeedback(
    const DrivingEffectsInputs&,DrivingEffectsState&,DrivingVisualEvents&);
} // namespace scr

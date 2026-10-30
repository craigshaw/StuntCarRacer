#ifndef SCR_MOTION_TRANSFORMS_H
#define SCR_MOTION_TRANSFORMS_H
#include "physics/transform_matrices.h"
#include <array>
#include <cstdint>

namespace scr {
// Original immutable byte table [0x1ec46,0x1ec5c). Its entries select words
// from the coefficient block; it is not itself a matrix of numeric values.
using MotionTransformSelectors=std::array<std::uint8_t,22>;
struct MotionTransformInputs {
    std::array<std::uint16_t,3> worldSpeed{}; // 0x1bcea/ec/ee
    std::array<std::uint16_t,3> adjustedAngles{}; // roll/pitch/yaw: 0x1bd32/34/36
    std::array<std::uint16_t,3> rotationSpeed{}; // 0x1bcf0/f2/f4
};
struct MotionTransformState {
    std::array<std::uint16_t,3> localVelocity{}; // 0x1bd2c/2e/30; Y is retained
    std::array<std::uint16_t,3> worldAcceleration{}; // 0x1bcf6/f8/fa
    std::array<std::uint16_t,3> transformedAngularVelocity{}; // 0x1bd3a/3c/3e
    std::uint16_t baseTargetRoll{},baseRollAngle{},additionalYawOffset{}; // 0x1bd0e/10/12
};

// Signed Q15 multiplication with the original ASL.L/SWAP word result. Every
// product is rounded down and truncated BEFORE a transformed-coordinate sum;
// moving the rounding after a conventional dot product changes the game.
[[nodiscard]] std::uint16_t multiplyMatrixWord(std::uint16_t value,std::uint16_t coefficient) noexcept;

// Original [0x6158c,0x615e6): transforms only X/Z and preserves localVelocity Y.
// In the original routines that load selector bytes into D1, bits 8..15 of D1
// must be zero on entry. This native API expresses that normal table-index
// convention with bytes. Any used selector >=36 throws std::out_of_range.
void transformWorldCoordinates(const MotionTransformInputs& inputs,const TransformMatrices& matrices,
                               const MotionTransformSelectors& selectors,MotionTransformState& state);

// Original [0x615e6,0x61618) invokes getNegativeTrigValue/getPositiveTrigValue:
// those helpers scale selected coefficients by -317/+317, without trig calls.
// Names follow the matched source; physical meaning of these terms is pending.
void calculateViewAngles(const TransformMatrices& matrices,MotionTransformState& state) noexcept;

// Original [0x61618,0x61672): three independently rounded products per output.
void calculateSecondaryCoordinates(const MotionTransformInputs& inputs,const TransformMatrices& matrices,
                                   const MotionTransformSelectors& selectors,MotionTransformState& state);

// Original [0x61672,0x616d6): two products for X/Y; Z adds raw rotationSpeed Z
// to the newly transformed Y scaled by coefficient word 4.
void calculateTertiaryCoordinates(const MotionTransformInputs& inputs,const TransformMatrices& matrices,
                                  const MotionTransformSelectors& selectors,MotionTransformState& state);
} // namespace scr
#endif

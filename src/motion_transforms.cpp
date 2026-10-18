#include "motion_transforms.h"
#include <stdexcept>

namespace scr {
namespace {
std::int32_t signedWord(std::uint16_t word) noexcept {
    return word<0x8000 ? std::int32_t(word) : std::int32_t(word)-0x10000;
}
std::uint16_t selectedProduct(std::uint16_t value,std::uint8_t selector,const TransformMatrices& matrices) {
    if(selector>=matrices.words.size()) throw std::out_of_range("Motion transform selector outside coefficient block");
    return multiplyMatrixWord(value,matrices.words[selector]);
}
}
std::uint16_t multiplyMatrixWord(std::uint16_t value,std::uint16_t coefficient) noexcept {
    const auto product=std::uint32_t(signedWord(value)*signedWord(coefficient));
    return std::uint16_t((product<<1)>>16);
}
void transformWorldCoordinates(const MotionTransformInputs& input,const TransformMatrices& matrices,
                               const MotionTransformSelectors& selectors,MotionTransformState& state) {
    for(unsigned row:{2u,0u}) {
        std::uint16_t sum=0;
        for(unsigned column=0;column<3;++column)
            sum=std::uint16_t(sum+selectedProduct(input.worldSpeed[column],selectors[row+column*3],matrices));
        state.localVelocity[row]=sum;
    }
}
void calculateViewAngles(const TransformMatrices& matrices,MotionTransformState& state) noexcept {
    state.baseRollAngle=multiplyMatrixWord(0xfec3,matrices.words[15]);
    state.additionalYawOffset=multiplyMatrixWord(0xfec3,matrices.words[4]);
    state.baseTargetRoll=multiplyMatrixWord(0x013d,matrices.words[14]);
}
void calculateSecondaryCoordinates(const MotionTransformInputs& input,const TransformMatrices& matrices,
                                   const MotionTransformSelectors& selectors,MotionTransformState& state) {
    for(unsigned step=0;step<3;++step) {
        const unsigned row=2-step; std::uint16_t sum=0;
        for(unsigned column=0;column<3;++column)
            sum=std::uint16_t(sum+selectedProduct(input.adjustedAngles[column],selectors[9+row+column*3],matrices));
        state.worldAcceleration[row]=sum;
    }
}
void calculateTertiaryCoordinates(const MotionTransformInputs& input,const TransformMatrices& matrices,
                                  const MotionTransformSelectors& selectors,MotionTransformState& state) {
    for(unsigned step=0;step<2;++step) {
        const unsigned row=1-step;
        const auto first=selectedProduct(input.rotationSpeed[0],selectors[18+row],matrices);
        const auto second=selectedProduct(input.rotationSpeed[1],selectors[20+row],matrices);
        state.transformedAngularVelocity[row]=std::uint16_t(first+second);
    }
    state.transformedAngularVelocity[2]=std::uint16_t(
        multiplyMatrixWord(state.transformedAngularVelocity[1],matrices.words[4])+input.rotationSpeed[2]);
}
} // namespace scr

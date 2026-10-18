#include "motion_integration.h"

namespace scr {
namespace {

constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value<0x8000 ? std::int32_t(value) : std::int32_t(value)-0x10000;
}

constexpr std::int32_t signedByte(std::uint8_t value) noexcept {
    return value<0x80 ? std::int32_t(value) : std::int32_t(value)-0x100;
}

constexpr std::int32_t timeScaled(std::uint16_t value) noexcept {
    // MULS.W by238 followed by ASR.L#8; C++17 division needs explicit
    // negative rounding. The result fits a signed word for every input.
    const auto product=signedWord(value)*238;
    return product/256-(product%256<0 ? 1 : 0);
}

void integrate(const MotionVector& acceleration,MotionVector& velocity) noexcept {
    for(unsigned axis=0;axis<3;++axis)
        velocity[axis]=static_cast<std::uint16_t>(std::int32_t(velocity[axis])+timeScaled(acceleration[axis]));
}

void clampAngle(std::uint16_t positiveLimit,std::uint16_t negativeLimit,
                std::uint16_t& angle,std::uint16_t& rotation) noexcept {
    const bool negative=(angle&0x8000u)!=0;
    const auto limit=negative ? negativeLimit : positiveLimit;
    // Original BCS on the negative path is strict: equality enters the clamp
    // and can clear rotation. Positive equality takes the no-clamp branch.
    const bool clamp=negative ? limit>=angle : limit<angle;
    if(!clamp) return;
    angle=limit;
    if(((limit^rotation)&0x8000u)==0) rotation=0;
}

} // namespace

void integrateWorldVelocity(const MotionVector& acceleration,MotionIntegrationState& state) noexcept {
    integrate(acceleration,state.worldVelocity);
}

void integrateAngularVelocity(const MotionVector& acceleration,MotionIntegrationState& state) noexcept {
    integrate(acceleration,state.rotationVelocity);
}

void updateWorldPosition(const MotionPositionInputs& inputs,const MotionAngleLimits& limits,
                         MotionIntegrationState& state) noexcept {
    for(unsigned axis=0;axis<3;++axis) {
        const auto displacement=timeScaled(state.worldVelocity[axis])*(axis==1 ? 128 : 64);
        state.worldPosition[axis]+=static_cast<std::uint32_t>(displacement);
    }
    // MOVE.W at the longword address clamps only its signed HIGH word. Keep
    // the fractional low word, including when the position is negative.
    const auto heightInteger=static_cast<std::uint16_t>(state.worldPosition[1]>>16);
    if(signedWord(heightInteger)>=1000)
        state.worldPosition[1]=(state.worldPosition[1]&0xffffu)|(1000u<<16);

    for(unsigned axis=0;axis<3;++axis)
        state.angles[axis]=static_cast<std::uint16_t>(std::int32_t(state.angles[axis])
                                                     +timeScaled(inputs.transformedAngularVelocity[axis]));
    const unsigned special=(inputs.raceCompletionFlag&0x80u) && inputs.renderingOrderMode==0xe0 ? 1 : 0;
    clampAngle(limits[special],limits[special+2],state.angles[0],state.rotationVelocity[0]);
    clampAngle(limits[special],limits[special+2],state.angles[2],state.rotationVelocity[2]);

    state.steepPitchFlag&=0x7f;
    auto pitchMagnitude=static_cast<std::uint8_t>(state.angles[0]>>8);
    if(signedByte(pitchMagnitude)<0)
        pitchMagnitude=static_cast<std::uint8_t>(0u-std::uint32_t(pitchMagnitude));
    if(signedByte(pitchMagnitude)>=15) state.steepPitchFlag|=0x80;
    state.viewportCenterY=static_cast<std::uint16_t>(0u-std::uint32_t(state.angles[0]));
}

} // namespace scr

#include "physics/wheel_contact.h"

namespace scr {
namespace {

constexpr std::int32_t signedByte(std::uint8_t value) noexcept {
    return value<0x80 ? std::int32_t(value) : std::int32_t(value)-0x100;
}

constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value<0x8000 ? std::int32_t(value) : std::int32_t(value)-0x10000;
}

constexpr std::int64_t signedLong(std::uint32_t value) noexcept {
    return value<0x80000000u ? std::int64_t(value) : std::int64_t(value)-0x100000000LL;
}

void adjustAtTrackEdge(const WheelContactInputs& inputs,WheelContactState& state) noexcept {
    auto edgeDistance=inputs.lateralPosition;
    if(signedWord(edgeDistance)>=0)
        edgeDistance=static_cast<std::uint16_t>(0x180u-edgeDistance);
    if(signedWord(edgeDistance)<0)
        edgeDistance=static_cast<std::uint16_t>(0u-std::uint32_t(edgeDistance));

    // NEG.W of 0x8000 remains negative; the signed comparison deliberately
    // admits it and the following byte mask produces distance zero.
    if(signedWord(edgeDistance)<=0x30) {
        const auto edgeDrop=std::uint32_t(edgeDistance&0xffu)*16u;
        const auto loweredHeight=state.sampledHeight-edgeDrop-0x100u;
        if(signedLong(loweredHeight)>=0x1000) {
            state.sampledHeight=loweredHeight;
            // MOVE.B from the word's address reads its big-endian high byte.
            state.bouncePhase=(((inputs.lateralPosition>>8)^inputs.reverseDirection)&0x80u) ? 0x80 : 0x40;
            return;
        }
    }

    state.sampledHeight=0x1000;
    state.renderingOrderMode=static_cast<std::uint8_t>((state.renderingOrderMode>>1)|0x80u);
}

} // namespace

void applyWheelHeightContact(std::uint32_t rawInterpolatedHeight,
                            const WheelContactInputs& inputs,
                            WheelContactState& state) noexcept {
    state.sampledHeight=rawInterpolatedHeight;
    const bool outside=(state.outOfBoundsFlag&0x80u)!=0;
    state.outOfBoundsFlag&=0x7f;
    if(outside) adjustAtTrackEdge(inputs,state);

    auto pitchMagnitude=inputs.pitchHighByte;
    if(signedByte(pitchMagnitude)<0)
        pitchMagnitude=static_cast<std::uint8_t>(0u-std::uint32_t(pitchMagnitude));
    if(signedByte(inputs.speedHighByte)<10 && signedByte(pitchMagnitude)<=5) {
        // ADD.L followed by ROXR.L uses the addition carry as bit 32. This is
        // an unsigned 33-bit average, including when either bit pattern is
        // negative as int32_t. An arithmetic average would change the result.
        state.height=static_cast<std::uint32_t>((std::uint64_t(state.height)+state.sampledHeight)>>1);
    } else {
        state.height=state.sampledHeight;
    }
}

} // namespace scr

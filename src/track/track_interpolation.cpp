#include "track/track_interpolation.h"

namespace scr {
namespace {

constexpr std::int32_t signedWord(std::uint16_t word) noexcept {
    return word<0x8000 ? std::int32_t(word) : std::int32_t(word)-0x10000;
}

// Explicit arithmetic right shift: portable in C++17 for negative operands.
constexpr std::int64_t shiftDown(std::int64_t value,unsigned bits) noexcept {
    const std::int64_t divisor=std::int64_t(1)<<bits;
    return value/divisor-(value%divisor<0 ? 1 : 0);
}

constexpr std::int32_t interpolateEdge(std::uint16_t start,std::uint16_t end,
                                     std::uint8_t fraction) noexcept {
    // SUB.W precedes MULS: the endpoint difference wraps as a word before its
    // signed interpretation. The widened sum fits int32_t for every input.
    const auto difference=static_cast<std::uint16_t>(std::int32_t(end)-start);
    return signedWord(start)*256+signedWord(difference)*fraction;
}

std::int64_t blendDisplacement(std::int32_t difference,std::uint8_t fraction) noexcept {
    // Large deltas are shifted before multiplication. Only the resulting low
    // word reaches MULU, including its sign test, even if the full delta would
    // not fit that word. This observable wraparound is retained deliberately.
    const bool coarse=difference>=32768 || difference<=-32768;
    const auto word=static_cast<std::uint16_t>(coarse ? shiftDown(difference,3) : difference);
    std::uint32_t product;
    if(word&0x8000) {
        const auto magnitude=static_cast<std::uint16_t>(0u-std::uint32_t(word));
        product=std::uint32_t(magnitude)*fraction;
        // MOVE.B #0,D0 precedes NEG.L on this branch, discarding the low byte
        // before changing the sign. Positive deltas do not take that rounding.
        product=0u-(product&0xffffff00u);
    } else {
        product=std::uint32_t(word)*fraction;
    }
    if(coarse) product<<=3;
    const auto signedProduct=product<0x80000000u ? std::int64_t(product)
                                              : std::int64_t(product)-0x100000000LL;
    return shiftDown(signedProduct,8);
}

} // namespace

std::uint32_t interpolateTrackCoordinate(const TrackInterpolationInputs& inputs) noexcept {
    const auto nearEdge=interpolateEdge(inputs.control1,inputs.control2,inputs.lateral);
    const auto farEdge=interpolateEdge(inputs.control3,inputs.control4,inputs.lateral);
    const auto difference=farEdge-nearEdge; // bounded by two signed-word edge interpolations
    return static_cast<std::uint32_t>(std::int64_t(nearEdge)+blendDisplacement(difference,inputs.blend));
}

} // namespace scr

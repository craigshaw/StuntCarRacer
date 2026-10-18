#ifndef SCR_STEERING_RESPONSE_H
#define SCR_STEERING_RESPONSE_H
#include "track_sampling.h"
#include <array>
#include <cstdint>

namespace scr {
using SteeringOffsetTable=std::array<std::uint16_t,3>; // Original [0x6125a,0x61260).
struct SteeringResponseInputs {
    std::uint16_t targetAngle{},forwardVelocity{},yawVelocity{};
    std::uint8_t playerSegment{},trackProgression{},wheelMovementActive{};
    // Configuration writes only the high byte of the original word at 0x1bc32.
    // Steering XORs that whole word, so the adjacent retained byte participates.
    std::uint8_t reverseDirectionLowByte{}; // RAM 0x1bc33
    std::uint32_t integrityWord{}; // Runtime-patched RAM 0x64aec, not disk bytes.
};
struct SteeringResponseState {
    TrackSegmentConfiguration segment{};
    std::uint8_t currentSegment{},steeringDirection{},directionMultiplier{},scratch3{},scratch4{};
    std::uint16_t yawAngle{},progressDistance{},steeringScale{},angleDeltaScratch{},yawAcceleration{};
};

// Original [0x61012,0x6125a), including its segment loader and optional advance.
// Preserves original byte/word wrapping, direct yaw correction (the enhanced
// source adds a timestep multiply), and integrity gate: acceleration is zero
// unless integrityWord == 0x9cedcd02 and wheelMovementActive is nonzero.
// Normal original calling convention requires D1/D2 bits8..15 zero. Native
// inputs use bounded segment indices; invalid track metadata throws, as in the
// shared configuration loader. CPU registers/CCR and timing are not outputs.
void calculateSteeringResponse(const TrackGeometryData& data,const SteeringOffsetTable& offsets,
                              const SteeringResponseInputs& inputs,SteeringResponseState& state);
} // namespace scr
#endif

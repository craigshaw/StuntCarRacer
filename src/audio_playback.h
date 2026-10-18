#pragma once
#include <array>
#include <cstdint>

namespace scr {
struct SampleParameters {
    std::uint32_t address{}, byteLength{};
    std::uint16_t period{}, volume{}, channel{};
};
struct AudioSampleData {
    std::array<SampleParameters, 8> samples{};
    std::array<std::uint16_t, 4> channelMasks{};
};
struct AudioPlaybackState {
    std::array<std::uint16_t, 4> completedInterrupts{};
    std::uint16_t dmaEnableGuard{};
};
struct AudioRegisterWrite {
    std::uint32_t address{};
    std::uint16_t value{};
};
using AudioRegisterWrites = std::array<AudioRegisterWrite, 10>;

// Original playSample [0xf362,0xf3ee). Return the ordered 16-bit Paula bus
// writes, including the two halves of the sample pointer. No audio is rendered
// here. The caller can consume these effects in a native sound backend.
// Sample IDs wrap modulo eight; supplied channels must be in [0,3].
[[nodiscard]] AudioRegisterWrites requestSample(std::uint16_t sampleId,
    const AudioSampleData& data, AudioPlaybackState& state);
} // namespace scr

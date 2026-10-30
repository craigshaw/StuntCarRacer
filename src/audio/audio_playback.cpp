#include "audio/audio_playback.h"
#include <stdexcept>

namespace scr {
AudioRegisterWrites requestSample(std::uint16_t sampleId,
    const AudioSampleData& data, AudioPlaybackState& state) {
    const auto& sample = data.samples[sampleId & 7];
    if (sample.channel >= 4) throw std::out_of_range("Sample channel outside Paula's four channels");
    const auto mask = data.channelMasks[sample.channel];
    const auto interruptMask = static_cast<std::uint16_t>(mask << 7);
    const auto channelAddress = 0xdff0a0u + sample.channel * 16u;
    state.completedInterrupts[sample.channel] = 0;
    // The original sets this guard while enabling DMA, then clears it before
    // returning. Interrupt interleaving is outside this synchronous contract.
    state.dmaEnableGuard = 0;
    return {{{0xdff09a, interruptMask}, {0xdff096, mask},
             {channelAddress, static_cast<std::uint16_t>(sample.address >> 16)},
             {channelAddress + 2, static_cast<std::uint16_t>(sample.address)},
             {channelAddress + 4, static_cast<std::uint16_t>(sample.byteLength >> 1)},
             {channelAddress + 8, sample.volume}, {channelAddress + 6, sample.period},
             {0xdff09c, interruptMask},
             {0xdff09a, static_cast<std::uint16_t>(interruptMask | 0x8000)},
             {0xdff096, static_cast<std::uint16_t>(mask | 0x8000)}}};
}
} // namespace scr

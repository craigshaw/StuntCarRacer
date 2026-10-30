#pragma once
#include "audio/audio_playback.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scr {
struct EngineSampleLevel {
    std::uint32_t address{}, wordLength{};
};
struct EngineAudioData {
    // Original plus seven successively decimated levels. The pitch routine
    // selects levels 0..6; initialization also creates the eighth record.
    std::array<EngineSampleLevel,8> levels{};
    std::vector<std::uint8_t> generatedSamples;
};
inline constexpr std::uint32_t originalEngineSampleAddress=0x19de6;
inline constexpr std::size_t originalEngineSampleBytes=3172;
inline constexpr std::uint32_t generatedEngineSampleAddress=0x1aa4a;
// Original generateEngineSamples [0xf402,0xf472). Source bytes are unsigned
// storage for signed Amiga PCM; no sample values are embedded in the port.
[[nodiscard]] EngineAudioData generateEngineSamples(const std::uint8_t* sample,
    std::size_t bytes);
struct EngineAudioState {
    std::uint16_t pitchAccumulator{}, savedPeriod{};
};
struct EngineAudioInputs {
    std::uint16_t pitchDelta{}, dmaControl{};
    std::uint8_t noiseFlag{}, gameExit{};
};
// Audio-only tail of original PAL verticalBlank [0x6171a,0x617c4).
// Call once per 50 Hz VBlank, separately from the six-VBlank gameplay step.
[[nodiscard]] std::vector<AudioRegisterWrite> updateEngineAudio(
    const EngineAudioData&,const EngineAudioInputs&,EngineAudioState&);
// Channel 0 installs the saved pitch at every DMA interrupt. Channels 1..3
// count startup and completion interrupts, stopping after the second event.
// Inputs are DMA readback and the original four channel masks; the guard is
// retained in AudioPlaybackState for the original enable-race behavior.
[[nodiscard]] std::vector<AudioRegisterWrite> completeAudioChannel(
    unsigned channel,std::uint16_t dmaControl,
    const std::array<std::uint16_t,4>& masks,AudioPlaybackState&,
    std::uint16_t savedEnginePeriod);
} // namespace scr

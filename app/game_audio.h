#pragma once
#include "audio_playback.h"
#include "engine_audio.h"
#include "paula_mixer.h"
#include <cstdint>
#include <vector>

namespace scr::viewer {
// A copy of the original sound commands and pitch inputs. This never exposes
// the simulation's writable RAM to the audio thread.
struct GameAudioFrame {
    std::vector<AudioRegisterWrite> writes;
    std::uint16_t forwardSpeed{}, dmaControl{};
    std::uint8_t wheelMovement{}, inputFlags{}, noiseFlag{}, gameExit{};
    unsigned restarts{}, action{};
};
struct GameAudioSeed {
    std::vector<std::uint8_t> samples;
    AudioSampleData parameters;
    AudioPlaybackState playback;
    EngineAudioState engine;
    std::uint16_t pitchDelta{};
    GameAudioFrame frame;
};
struct GameAudioStatistics {
    std::uint64_t frames{}, vblanks{}, interrupts{}, effectStarts{};
    std::uint16_t minimumPeriod=0xffff,maximumPeriod{};
};
class GameAudio {
public:
    explicit GameAudio(const GameAudioSeed&);
    void submit(const GameAudioFrame&);
    [[nodiscard]] std::vector<std::int16_t> renderFrames(std::size_t);
    const GameAudioStatistics& statistics()const noexcept{return statistics_;}
    const EngineAudioState& engineState()const noexcept{return engine_;}
private:
    PaulaMixer mixer_;
    EngineAudioData data_;
    AudioPlaybackState playback_;
    EngineAudioState engine_;
    AudioSampleData parameters_;
    GameAudioFrame frame_;
    std::uint16_t delta_{};
    unsigned untilVblank_{};
    GameAudioStatistics statistics_;
    void vblank();
    void apply(const std::vector<AudioRegisterWrite>&);
};
} // namespace scr::viewer

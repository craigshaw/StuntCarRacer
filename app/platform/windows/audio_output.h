#pragma once
#include "audio/game_audio.h"
#include <memory>
#include <string>

namespace scr::runtime {
// All device operations and PCM rendering run on one worker. Public methods
// copy small command packets under its lock; they never call a CPU emulator.
class WindowsAudioOutput {
public:
    explicit WindowsAudioOutput(const GameAudioSeed&);
    ~WindowsAudioOutput();
    WindowsAudioOutput(const WindowsAudioOutput&)=delete;
    WindowsAudioOutput& operator=(const WindowsAudioOutput&)=delete;
    void submit(const GameAudioFrame&);
    void reset(const GameAudioSeed&);
    void setPlaying(bool);
    void setMuted(bool);
    std::string error()const;
    std::uint64_t completedBuffers()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace scr::runtime

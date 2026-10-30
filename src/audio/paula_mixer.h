#pragma once
#include "audio/audio_playback.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace scr {
// Native playback of the game's four sample-DMA channels. This is not a
// cycle-level chipset emulator: DMA contention, attach/manual modes, Paula's
// volume PWM and the Amiga's analog filters are outside this contract.
//
// PAL periods use 3,546,895 ticks/second (Commodore Hardware Reference Manual,
// Audio Hardware). Exact integer integration of the held samples resamples
// those clocks to host PCM; render call boundaries do not change the result.
class PaulaMixer {
public:
    using InterruptCallback = std::function<void(unsigned channel)>;
    static constexpr std::uint32_t palClock = 3546895;

    explicit PaulaMixer(std::vector<std::uint8_t> chipRam,
        std::uint32_t sampleRate = 48000);
    // Reset all registers/playback and replace the immutable sample image.
    // The selected sample rate and interrupt callback are retained.
    void reset(std::vector<std::uint8_t> chipRam);
    void setInterruptCallback(InterruptCallback callback);
    void apply(std::uint32_t address, std::uint16_t value);
    void apply(const AudioRegisterWrite& write) { apply(write.address, write.value); }
    void apply(const AudioRegisterWrites& writes);
    // Stereo signed PCM: channels 0+3 left, 1+2 right. Two full-volume voices
    // per side fit int16 without clipping. A stopped DMA voice contributes 0.
    [[nodiscard]] std::vector<std::int16_t> renderFrames(std::size_t count);
    [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::uint16_t dmaControl() const noexcept { return dmaControl_; }
    [[nodiscard]] std::uint16_t interruptEnable() const noexcept { return interruptEnable_; }
    [[nodiscard]] std::uint16_t interruptRequest() const noexcept { return interruptRequest_; }
    [[nodiscard]] bool active(unsigned channel) const;

private:
    struct Channel {
        std::uint32_t location{}, bytesRemaining{};
        std::uint64_t cursor{};
        std::uint16_t length{}, period{}, volume{};
        std::uint64_t remainingUnits{}, generation{};
        int sample{};
        bool active{};
    };
    std::vector<std::uint8_t> chipRam_;
    std::uint32_t sampleRate_;
    std::array<Channel, 4> channels_{};
    std::uint16_t dmaControl_{}, interruptEnable_{}, interruptRequest_{};
    std::uint16_t deliveredInterrupts_{};
    InterruptCallback callback_;
    bool dispatching_{};

    void reconcileDma();
    void startBlock(unsigned channel);
    void advanceSample(unsigned channel);
    void requestInterrupt(unsigned channel);
    void dispatchInterrupts();
    [[nodiscard]] std::uint64_t periodUnits(const Channel& channel) const noexcept;
    [[nodiscard]] int readSample(std::uint64_t address) const noexcept;
    [[nodiscard]] static int volume(const Channel& channel) noexcept;
};
} // namespace scr

#include "paula_mixer.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace scr {
namespace {
constexpr std::uint16_t audioInterruptMask = 0x0780;
void setClear(std::uint16_t& reg, std::uint16_t value) {
    const auto bits = static_cast<std::uint16_t>(value & 0x7fff);
    reg = static_cast<std::uint16_t>((value & 0x8000) ? reg | bits : reg & ~bits);
}
}

PaulaMixer::PaulaMixer(std::vector<std::uint8_t> chipRam, std::uint32_t sampleRate)
    : chipRam_(std::move(chipRam)), sampleRate_(sampleRate) {
    if (!sampleRate_) throw std::invalid_argument("Audio sample rate must be nonzero");
}

void PaulaMixer::reset(std::vector<std::uint8_t> chipRam) {
    if (dispatching_) throw std::logic_error("Cannot reset mixer inside an audio interrupt");
    chipRam_ = std::move(chipRam);
    channels_ = {};
    dmaControl_ = interruptEnable_ = interruptRequest_ = deliveredInterrupts_ = 0;
}

void PaulaMixer::setInterruptCallback(InterruptCallback callback) {
    callback_ = std::move(callback);
    dispatchInterrupts();
}

bool PaulaMixer::active(unsigned channel) const {
    if (channel >= channels_.size()) throw std::out_of_range("Audio channel outside [0,3]");
    return channels_[channel].active;
}

void PaulaMixer::apply(const AudioRegisterWrites& writes) {
    for (const auto& write : writes) apply(write);
}

void PaulaMixer::apply(std::uint32_t address, std::uint16_t value) {
    switch (address) {
    case 0xdff096:
        setClear(dmaControl_, value);
        reconcileDma();
        return;
    case 0xdff09a:
        setClear(interruptEnable_, value);
        dispatchInterrupts();
        return;
    case 0xdff09c:
        setClear(interruptRequest_, value);
        // Acknowledging a request permits the next block's request to be
        // delivered. An unacknowledged level is delivered once, not spun on.
        deliveredInterrupts_ &= interruptRequest_;
        dispatchInterrupts();
        return;
    default:
        break;
    }
    if (address < 0xdff0a0 || address >= 0xdff0e0 || (address & 1)) return;
    auto& channel = channels_[(address - 0xdff0a0) / 16];
    switch (address & 15) {
    case 0: channel.location = (channel.location & 0xffffu) | (std::uint32_t(value) << 16); break;
    case 2: channel.location = (channel.location & 0xffff0000u) | (value & 0xfffeu); break;
    case 4: channel.length = value; break;
    case 6: channel.period = value; break;
    case 8: channel.volume = value; break;
    default: break;
    }
}

void PaulaMixer::reconcileDma() {
    for (unsigned i = 0; i < channels_.size(); ++i) {
        auto& channel = channels_[i];
        const bool enabled = (dmaControl_ & 0x0200) && (dmaControl_ & (1u << i));
        if (enabled == channel.active) continue;
        ++channel.generation;
        channel.active = enabled;
        if (enabled) startBlock(i);
        else {
            channel.sample = 0;
            channel.remainingUnits = 0;
        }
    }
}

std::uint64_t PaulaMixer::periodUnits(const Channel& channel) const noexcept {
    // A zero 16-bit down counter represents its full 65,536-tick range.
    const std::uint32_t period = channel.period ? channel.period : 65536u;
    return std::uint64_t(period) * sampleRate_;
}

int PaulaMixer::readSample(std::uint64_t address) const noexcept {
    // Invalid addresses do not wrap into another part of the supplied image.
    if (address >= chipRam_.size()) return 0;
    const auto byte = chipRam_[address];
    return byte < 128 ? int(byte) : int(byte) - 256;
}

int PaulaMixer::volume(const Channel& channel) noexcept {
    return (channel.volume & 0x40) ? 64 : channel.volume & 0x3f;
}

void PaulaMixer::startBlock(unsigned i) {
    auto& channel = channels_[i];
    const auto generation = channel.generation;
    channel.cursor = channel.location;
    channel.bytesRemaining = (channel.length ? std::uint32_t(channel.length) : 65536u) * 2u;
    // The hardware copies LC/LEN before raising its start/reload interrupt.
    // A handler can therefore queue the following block without replacing the
    // one that is about to play. It can also stop DMA or change the period.
    requestInterrupt(i);
    if (!channel.active || generation != channel.generation) return;
    channel.sample = readSample(channel.cursor);
    channel.remainingUnits = periodUnits(channel);
}

void PaulaMixer::advanceSample(unsigned i) {
    auto& channel = channels_[i];
    if (!channel.active) return;
    if (--channel.bytesRemaining == 0) startBlock(i);
    else {
        ++channel.cursor;
        channel.sample = readSample(channel.cursor);
        channel.remainingUnits = periodUnits(channel);
    }
}

void PaulaMixer::requestInterrupt(unsigned i) {
    interruptRequest_ |= static_cast<std::uint16_t>(0x80u << i);
    dispatchInterrupts();
}

void PaulaMixer::dispatchInterrupts() {
    if (dispatching_ || !callback_ || !(interruptEnable_ & 0x4000)) return;
    dispatching_ = true;
    try {
        // Reentrant register writes may expose another pending channel. An
        // acknowledged request can fire again only on a new DMA block event.
        for (;;) {
            const auto ready = static_cast<std::uint16_t>(interruptRequest_ &
                interruptEnable_ & audioInterruptMask & ~deliveredInterrupts_);
            if (!ready || !(interruptEnable_ & 0x4000)) break;
            unsigned channel = 0;
            while (!(ready & (0x80u << channel))) ++channel;
            deliveredInterrupts_ |= static_cast<std::uint16_t>(0x80u << channel);
            callback_(channel);
        }
    } catch (...) {
        dispatching_ = false;
        throw;
    }
    dispatching_ = false;
}

std::vector<std::int16_t> PaulaMixer::renderFrames(std::size_t count) {
    if (dispatching_) throw std::logic_error("Cannot render recursively inside an audio interrupt");
    if (count > std::vector<std::int16_t>().max_size() / 2)
        throw std::length_error("Audio frame count is too large");
    std::vector<std::int16_t> output(count * 2);
    for (std::size_t frame = 0; frame < count; ++frame) {
        std::uint64_t remaining = palClock;
        std::int64_t integrated[2]{};
        while (remaining) {
            auto step = remaining;
            for (const auto& channel : channels_)
                if (channel.active) step = std::min(step, channel.remainingUnits);
            int amplitudes[2]{};
            for (unsigned i = 0; i < channels_.size(); ++i) {
                const auto& channel = channels_[i];
                if (channel.active)
                    amplitudes[(i == 0 || i == 3) ? 0 : 1] += channel.sample * volume(channel) * 2;
            }
            for (unsigned side = 0; side < 2; ++side)
                integrated[side] += std::int64_t(amplitudes[side]) * step;
            for (auto& channel : channels_)
                if (channel.active) channel.remainingUnits -= step;
            remaining -= step;
            // Advance all channels on the same clock. Processing complete host
            // frames independently per voice would incorrectly move IRQ writes
            // to the other voices back to the start of the host frame.
            for (unsigned i = 0; i < channels_.size(); ++i)
                if (channels_[i].active && !channels_[i].remainingUnits) advanceSample(i);
        }
        for (unsigned side = 0; side < 2; ++side)
            output[frame * 2 + side] = static_cast<std::int16_t>(integrated[side] / palClock);
    }
    return output;
}
} // namespace scr

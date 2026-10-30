// Synthetic native mixer tests. No original sample bytes and no claim of
// cycle/analog equality: these exercise the stated sample-DMA/PCM contract.
#include "audio/paula_mixer.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void configure(scr::PaulaMixer& mixer, unsigned channel, std::uint32_t address,
    std::uint16_t words = 1, std::uint16_t period = 1, std::uint16_t volume = 64) {
    const auto base = 0xdff0a0u + channel * 16;
    mixer.apply(base, std::uint16_t(address >> 16));
    mixer.apply(base + 2, std::uint16_t(address));
    mixer.apply(base + 4, words);
    mixer.apply(base + 6, period);
    mixer.apply(base + 8, volume);
}
void start(scr::PaulaMixer& mixer, unsigned mask = 1) {
    mixer.apply(0xdff096, std::uint16_t(0x8200 | mask));
}
void acknowledge(scr::PaulaMixer& mixer, unsigned channel) {
    mixer.apply(0xdff09c, std::uint16_t(0x80u << channel));
}
void expectLeft(const std::vector<std::int16_t>& pcm, const std::vector<int>& expected,
    const char* message) {
    require(pcm.size() == expected.size() * 2, "Incorrect stereo PCM size");
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (pcm[i * 2] != expected[i] || pcm[i * 2 + 1] != 0)
            throw std::runtime_error(std::string(message) + " at frame " + std::to_string(i));
}
}

int main() try {
    unsigned cases = 0;
    {
        scr::PaulaMixer mixer({128, 127, 255, 0}, scr::PaulaMixer::palClock);
        configure(mixer, 0, 0, 2);
        start(mixer);
        expectLeft(mixer.renderFrames(4), {-16384, 16256, -128, 0},
            "Signed samples/word byte order changed");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 10, 20, 20, 30, 30, 216, 216}, scr::PaulaMixer::palClock);
        for (unsigned i = 0; i < 4; ++i) configure(mixer, i, i * 2);
        start(mixer, 15);
        require(mixer.renderFrames(1) == std::vector<std::int16_t>({-3840, 6400}),
            "Original channel stereo assignment changed");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({128, 128}, scr::PaulaMixer::palClock);
        for (unsigned i = 0; i < 4; ++i) configure(mixer, i, 0);
        start(mixer, 15);
        require(mixer.renderFrames(2) == std::vector<std::int16_t>({-32768, -32768, -32768, -32768}),
            "Full-volume voices clipped or wrapped");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20, 30, 40, 50, 60}, scr::PaulaMixer::palClock);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) {
            ++interrupts;
            acknowledge(mixer, channel);
            if (interrupts == 1) configure(mixer, channel, 2, 2);
        });
        mixer.apply(0xdff09a, 0xc080);
        configure(mixer, 0, 0);
        start(mixer);
        require(interrupts == 1, "DMA startup did not request an interrupt");
        expectLeft(mixer.renderFrames(8), {1280, 2560, 3840, 5120, 6400, 7680, 3840, 5120},
            "LC/LEN changes replaced the currently latched block");
        require(interrupts == 3, "Reload IRQ did not follow the latched block length");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20}, scr::PaulaMixer::palClock);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) {
            ++interrupts;
            acknowledge(mixer, channel);
            if (interrupts == 2) {
                mixer.apply(0xdff096, 1);
                mixer.apply(0xdff09a, 0x80);
            }
        });
        mixer.apply(0xdff09a, 0xc080);
        configure(mixer, 0, 0);
        start(mixer);
        expectLeft(mixer.renderFrames(5), {1280, 2560, 0, 0, 0},
            "One-shot stopped early or repeated after its second interrupt");
        require(interrupts == 2 && !mixer.active(0), "One-shot IRQ stop did not disable DMA");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 10}, scr::PaulaMixer::palClock);
        configure(mixer, 0, 0);
        mixer.apply(0xdff096, 0x8001);
        expectLeft(mixer.renderFrames(1), {0}, "Channel DMA bypassed master DMA");
        mixer.apply(0xdff096, 0x8200);
        expectLeft(mixer.renderFrames(1), {1280}, "Master enable lost channel enable");
        mixer.apply(0xdff096, 0x0200);
        expectLeft(mixer.renderFrames(1), {0}, "Master clear failed to stop channels");
        require((mixer.dmaControl() & 1) != 0, "Master clear cleared unrelated DMA bits");
        mixer.apply(0xdff096, 0x8200);
        mixer.apply(0xdff096, 1);
        expectLeft(mixer.renderFrames(1), {0}, "Channel clear failed");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 10}, scr::PaulaMixer::palClock);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) { require(channel == 0, "Wrong IRQ channel"); ++interrupts; });
        configure(mixer, 0, 0);
        start(mixer);
        require(interrupts == 0 && (mixer.interruptRequest() & 0x80), "Masked IRQ was not retained");
        mixer.apply(0xdff09a, 0x8080);
        require(interrupts == 0, "Audio IRQ bypassed interrupt master");
        mixer.apply(0xdff09a, 0xc000);
        require(interrupts == 1, "Enabling interrupt master did not dispatch pending IRQ");
        mixer.apply(0xdff09a, 0x8080);
        require(interrupts == 1, "Unacknowledged request dispatched twice");
        acknowledge(mixer, 0);
        require(mixer.interruptRequest() == 0, "INTREQ clear did not acknowledge request");
        const auto ignored = mixer.renderFrames(2); (void)ignored;
        require(interrupts == 2, "Next block failed to request a fresh IRQ");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20}, scr::PaulaMixer::palClock);
        configure(mixer, 0, 0, 1, 3);
        start(mixer);
        expectLeft(mixer.renderFrames(1), {1280}, "Initial period incorrect");
        mixer.apply(0xdff0a6, 1);
        mixer.apply(0xdff0a8, 32);
        expectLeft(mixer.renderFrames(4), {640, 640, 1280, 640},
            "Period update changed the in-flight countdown or volume was not immediate");
        mixer.apply(0xdff0a8, 0x7f);
        expectLeft(mixer.renderFrames(1), {2560}, "Volume bit 6 did not mean full scale");
        mixer.apply(0xdff0a8, 0x8000);
        expectLeft(mixer.renderFrames(1), {0}, "Unused volume bits affected output");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20}, scr::PaulaMixer::palClock);
        configure(mixer, 0, 0, 1, 0);
        start(mixer);
        const auto pcm = mixer.renderFrames(65537);
        require(pcm[2 * 65535] == 1280 && pcm[2 * 65536] == 2560,
            "Zero period was truncated instead of representing 65,536 ticks");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20}, scr::PaulaMixer::palClock);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) { ++interrupts; acknowledge(mixer, channel); });
        mixer.apply(0xdff09a, 0xc080);
        configure(mixer, 0, 0, 0);
        start(mixer);
        const auto before = mixer.renderFrames(131071); (void)before;
        require(interrupts == 1, "Zero length was truncated or reloaded early");
        const auto boundary = mixer.renderFrames(1); (void)boundary;
        require(interrupts == 2, "Zero length did not represent 65,536 words");
        ++cases;
    }
    {
        std::vector<std::uint8_t> memory(65540);
        memory[0] = memory[1] = 10;
        memory[65536] = memory[65537] = 20;
        scr::PaulaMixer mixer(memory, scr::PaulaMixer::palClock);
        memory[65536] = 99;
        configure(mixer, 0, 65537);
        start(mixer);
        expectLeft(mixer.renderFrames(1), {2560}, "Pointer high word, word alignment or immutable memory changed");
        mixer.apply(0xdff096, 1);
        configure(mixer, 0, 0xfffffffe, 2);
        start(mixer);
        expectLeft(mixer.renderFrames(6), {0, 0, 0, 0, 0, 0}, "Invalid sample pointer aliased valid memory");
        mixer.reset({10});
        configure(mixer, 0, 0);
        start(mixer);
        expectLeft(mixer.renderFrames(4), {1280, 0, 1280, 0}, "Memory boundary read did not return silence");
        ++cases;
    }
    {
        // Five Paula clocks per host frame. Channel 0 stops and silences
        // channel 1 at clock four. The final fifth of the frame must be silent
        // on both sides; independent whole-frame channel rendering gets this
        // inter-channel interrupt ordering wrong.
        scr::PaulaMixer mixer({10, 10, 20, 20}, scr::PaulaMixer::palClock / 5);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) {
            ++interrupts;
            acknowledge(mixer, channel);
            if (interrupts == 2) { mixer.apply(0xdff096, 1); mixer.apply(0xdff0b8, 0); }
        });
        mixer.apply(0xdff09a, 0xc080);
        configure(mixer, 0, 0, 1, 2);
        configure(mixer, 1, 2, 1, 100);
        start(mixer, 3);
        require(mixer.renderFrames(2) == std::vector<std::int16_t>({1024, 2048, 0, 0}),
            "IRQ updates to another voice moved away from their exact shared clock");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({1, 255}, scr::PaulaMixer::palClock / 5);
        configure(mixer, 0, 0);
        start(mixer);
        expectLeft(mixer.renderFrames(4), {25, -25, 25, -25}, "Held-sample area resampling changed");
        ++cases;
    }
    {
        const auto makeMemory = [] {
            std::vector<std::uint8_t> memory(4096);
            std::uint32_t random = 0x72f063a1;
            for (auto& value : memory) { random = random * 1664525u + 1013904223u; value = std::uint8_t(random >> 24); }
            return memory;
        };
        scr::PaulaMixer whole(makeMemory()), chunked(makeMemory());
        std::vector<unsigned> wholeEvents, chunkedEvents;
        const auto setup = [](scr::PaulaMixer& mixer, std::vector<unsigned>& events) {
            mixer.setInterruptCallback([&mixer, &events](unsigned channel) {
                events.push_back(channel);
                acknowledge(mixer, channel);
                const auto base = 0xdff0a0u + channel * 16;
                mixer.apply(base + 2, std::uint16_t((events.size() * 94) % 2048));
                mixer.apply(base + 4, std::uint16_t(1 + events.size() % 23));
                mixer.apply(base + 6, std::uint16_t(123 + events.size() % 301));
                mixer.apply(0xdff0a8 + ((channel + 1) % 4) * 16, std::uint16_t(events.size() % 65));
            });
            mixer.apply(0xdff09a, 0xc780);
            for (unsigned i = 0; i < 4; ++i) configure(mixer, i, i * 100, std::uint16_t(i + 1), std::uint16_t(123 + i * 117));
            start(mixer, 15);
        };
        setup(whole, wholeEvents); setup(chunked, chunkedEvents);
        const auto expected = whole.renderFrames(30001);
        std::vector<std::int16_t> actual;
        std::uint32_t random = 77;
        for (std::size_t remaining = 30001; remaining;) {
            random = random * 1664525u + 1013904223u;
            const auto count = std::min<std::size_t>(remaining, 1 + random % 139);
            const auto noFrames = chunked.renderFrames(0);
            require(noFrames.empty(), "Zero-frame rendering produced PCM");
            const auto part = chunked.renderFrames(count);
            actual.insert(actual.end(), part.begin(), part.end());
            remaining -= count;
        }
        require(actual == expected && wholeEvents == chunkedEvents,
            "Host callback chunking changed PCM or DMA interrupt order");
        require(wholeEvents.size() > 100, "Chunk comparison did not exercise enough loop reloads");
        ++cases;
    }
    {
        scr::PaulaMixer mixer({10, 20}, scr::PaulaMixer::palClock);
        unsigned interrupts = 0;
        mixer.setInterruptCallback([&](unsigned channel) { ++interrupts; acknowledge(mixer, channel); });
        configure(mixer, 0, 0);
        start(mixer);
        mixer.reset({30, 40});
        require(mixer.sampleRate() == scr::PaulaMixer::palClock && mixer.dmaControl() == 0 &&
            mixer.interruptEnable() == 0 && mixer.interruptRequest() == 0, "Reset did not clear registers");
        expectLeft(mixer.renderFrames(1), {0}, "Reset left a playing voice");
        mixer.apply(0xdff09a, 0xc080);
        configure(mixer, 0, 0);
        start(mixer);
        require(interrupts == 1, "Reset lost interrupt callback");
        expectLeft(mixer.renderFrames(1), {3840}, "Reset did not replace sample memory");
        ++cases;
    }
    {
        bool rejected = false;
        try { scr::PaulaMixer invalid({}, 0); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Zero host sample rate accepted");
        scr::PaulaMixer mixer({});
        rejected = false;
        try { const bool invalid = mixer.active(4); (void)invalid; }
        catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "Invalid channel index accepted");
        ++cases;
    }
    std::cout << "{\"total_cases\":" << cases << ",\"mismatches\":0,\"synthetic_only\":true,"
        "\"stereo_mapping\":true,\"signed_pcm\":true,\"latched_dma_blocks\":true,"
        "\"one_shot_irq_order\":true,\"setclr_master_gating\":true,"
        "\"shared_channel_clock\":true,\"chunk_independent\":true,"
        "\"bounds_checked\":true,\"cycle_analog_equality_claimed\":false}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

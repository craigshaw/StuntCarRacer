#include "engine_audio.h"
#include <stdexcept>

namespace scr {
EngineAudioData generateEngineSamples(const std::uint8_t* sample,std::size_t bytes) {
    if(!sample||bytes!=originalEngineSampleBytes)
        throw std::invalid_argument("Engine sample must contain the original3172bytes");
    EngineAudioData data;data.levels[0]={originalEngineSampleAddress,std::uint32_t(bytes/2)};
    // Reserve the original3200-byte work area, then retain only written data.
    data.generatedSamples.resize(3200);
    std::size_t count=bytes,destination=0,source=0,extent=0;
    for(unsigned level=1;level<data.levels.size();++level) {
        const auto start=destination;count>>=1;
        for(std::size_t i=0;i<count;++i) {
            const auto value=level==1?sample[source+2*i]:data.generatedSamples[source+2*i];
            data.generatedSamples[destination++]=value;
        }
        if(destination>extent)extent=destination;
        destination&=~std::size_t(1);
        data.levels[level]={generatedEngineSampleAddress+std::uint32_t(start),
            std::uint32_t((destination-start)/2)};
        source=start;
    }
    data.generatedSamples.resize(extent);return data;
}

std::vector<AudioRegisterWrite> updateEngineAudio(const EngineAudioData& data,
    const EngineAudioInputs& in,EngineAudioState& state) {
    std::vector<AudioRegisterWrite> writes{{0xdff096,in.dmaControl}};
    auto accumulator=std::uint16_t(state.pitchAccumulator+in.pitchDelta);
    if(accumulator&0x8000) {
        if(in.gameExit) {
            writes.push_back({0xdff096,1});writes.push_back({0xdff09a,0x80});return writes;
        }
        accumulator=0;
    }
    state.pitchAccumulator=accumulator;
    auto period=std::uint16_t(0x493e00u/std::uint16_t(accumulator+0x17a));
    if(period>=0x3fff)period=0x3ffe;
    period=std::uint16_t(period|in.noiseFlag);
    if(period<0x7c)period=0x7c;
    unsigned level=6;
    while(period>=0x100) {period>>=1;level=level?level-1:0;}
    const auto& sample=data.levels[level];
    writes.push_back({0xdff0a0,std::uint16_t(sample.address>>16)});
    writes.push_back({0xdff0a2,std::uint16_t(sample.address)});
    writes.push_back({0xdff0a4,std::uint16_t(sample.wordLength)});
    state.savedPeriod=period;return writes;
}

std::vector<AudioRegisterWrite> completeAudioChannel(unsigned channel,
    std::uint16_t dmaControl,const std::array<std::uint16_t,4>& masks,
    AudioPlaybackState& state,std::uint16_t savedEnginePeriod) {
    if(channel>=4)throw std::out_of_range("Audio interrupt channel outside0..3");
    if(!channel)return {{0xdff0a6,savedEnginePeriod}};
    const auto mask=masks[channel],interruptMask=std::uint16_t(mask<<7);
    if(!(dmaControl&mask)) {
        if(state.dmaEnableGuard&mask)return {};
        return {{0xdff09a,interruptMask}};
    }
    auto& count=state.completedInterrupts[channel];count=std::uint16_t(count+1);
    // BLT is signed after CMP.W #2: malformed/wrapped negative counters keep
    // running just as the original does; ordinary playback starts at zero.
    if((count&0x8000)||count<2)return {};
    return {{0xdff096,mask},{0xdff09a,interruptMask}};
}
} // namespace scr

#include "game_audio.h"
#include "driving_effects.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
std::vector<std::uint8_t> sampleImage(const GameAudioSeed& seed){
    auto bytes=seed.samples;
    if(bytes.size()<generatedEngineSampleAddress+3200)
        throw std::invalid_argument("Audio requires the captured Chip RAM sample image");
    const auto bank=generateEngineSamples(bytes.data()+originalEngineSampleAddress,originalEngineSampleBytes);
    // Recreate the original startup's decimated engine bank from local assets.
    std::copy(bank.generatedSamples.begin(),bank.generatedSamples.end(),bytes.begin()+generatedEngineSampleAddress);
    return bytes;
}
}
GameAudio::GameAudio(const GameAudioSeed& seed):mixer_(sampleImage(seed)),
    data_(generateEngineSamples(seed.samples.data()+originalEngineSampleAddress,originalEngineSampleBytes)),
    playback_(seed.playback),engine_(seed.engine),parameters_(seed.parameters),frame_(seed.frame),delta_(seed.pitchDelta){
    mixer_.setInterruptCallback([this](unsigned channel){
        ++statistics_.interrupts;
        // The original interrupt dispatcher acknowledges each audio source.
        mixer_.apply(0xdff09c,std::uint16_t(0x80u<<channel));
        apply(completeAudioChannel(channel,mixer_.dmaControl(),parameters_.channelMasks,playback_,engine_.savedPeriod));
    });
    mixer_.apply(0xdff096,0x8200);mixer_.apply(0xdff09a,0xc000);
    vblank();mixer_.apply(requestSample(7,parameters_,playback_));
}
void GameAudio::apply(const std::vector<AudioRegisterWrite>& writes){for(const auto& write:writes)mixer_.apply(write);}
void GameAudio::submit(const GameAudioFrame& frame){
    frame_=frame;
    // The verified headless trace deliberately has no asynchronous interrupts.
    // Recalculate only this audio copy's delta against its advancing50Hz pitch.
    DrivingEffectsInputs inputs{};DrivingEffectsState effects{};
    inputs.localForwardSpeed=frame.forwardSpeed;inputs.wheelMovementActive=frame.wheelMovement;
    inputs.inputFlags=frame.inputFlags;inputs.enginePitchAccumulator=engine_.pitchAccumulator;
    updateEnginePitch(inputs,effects);delta_=effects.enginePitchDelta;
    for(const auto& write:frame.writes){
        if(write.address==0xdff096&&!(write.value&0x8000)){
            for(unsigned channel=0;channel<4;++channel)if(write.value&(1u<<channel))playback_.completedInterrupts[channel]=0;
        }
        if(write.address==0xdff096&&(write.value&0x8000)&&(write.value&0xe))++statistics_.effectStarts;
        mixer_.apply(write);
    }
}
void GameAudio::vblank(){
    apply(updateEngineAudio(data_,{delta_,frame_.dmaControl,frame_.noiseFlag,frame_.gameExit},engine_));
    ++statistics_.vblanks;statistics_.minimumPeriod=std::min(statistics_.minimumPeriod,engine_.savedPeriod);
    statistics_.maximumPeriod=std::max(statistics_.maximumPeriod,engine_.savedPeriod);
    untilVblank_=mixer_.sampleRate()/50;
}
std::vector<std::int16_t> GameAudio::renderFrames(std::size_t count){
    std::vector<std::int16_t> result;result.reserve(count*2);
    while(count){if(!untilVblank_)vblank();const auto part=std::min<std::size_t>(count,untilVblank_);
        auto pcm=mixer_.renderFrames(part);result.insert(result.end(),pcm.begin(),pcm.end());
        count-=part;untilVblank_-=unsigned(part);statistics_.frames+=part;
    }
    // Application gain only; never change the Windows device volume.
    for(auto& sample:result)sample=std::int16_t(sample/2);
    return result;
}
} // namespace scr::viewer

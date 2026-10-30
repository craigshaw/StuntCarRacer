#pragma once
#include "audio/engine_audio.h"
#include "audio_playback_fixture.h"
namespace scr::test::engineAudio {
constexpr unsigned generationEntry=0xf402,generationEnd=0xf472;
constexpr unsigned interruptEntry=0xf2cc,interruptEnd=0xf350;
constexpr unsigned vblankEntry=0x6171a,vblankEnd=0x617c4;
constexpr unsigned parameterTable=0xf474,periodAddress=0xf472;
inline EngineAudioData readData(const std::uint8_t* ram) {
    EngineAudioData data;
    for(unsigned level=0;level<data.levels.size();++level)
        data.levels[level]={audio::longword(ram,parameterTable+8*level),
            audio::longword(ram,parameterTable+8*level+4)};
    return data;
}
inline void writeData(std::uint8_t* ram,const EngineAudioData& data) {
    for(unsigned level=0;level<data.levels.size();++level) {
        audio::putLong(ram,parameterTable+8*level,data.levels[level].address);
        audio::putLong(ram,parameterTable+8*level+4,data.levels[level].wordLength);
    }
}
inline EngineAudioInputs readInputs(const std::uint8_t* ram) {
    return {audio::word(ram,0x1bbf4),audio::word(ram,0x69ede),ram[0x5b646],ram[0x1bb63]};
}
inline void writeInputs(std::uint8_t* ram,const EngineAudioInputs& in) {
    audio::putWord(ram,0x1bbf4,in.pitchDelta);audio::putWord(ram,0x69ede,in.dmaControl);
    ram[0x5b646]=in.noiseFlag;ram[0x1bb63]=in.gameExit;
}
inline EngineAudioState readState(const std::uint8_t* ram) {
    return {audio::word(ram,0x1bc34),audio::word(ram,periodAddress)};
}
inline void writeState(std::uint8_t* ram,const EngineAudioState& state) {
    audio::putWord(ram,0x1bc34,state.pitchAccumulator);audio::putWord(ram,periodAddress,state.savedPeriod);
}
} // namespace scr::test::engineAudio

#pragma once
#include "audio_playback.h"
namespace scr::test::audio {
constexpr unsigned entry=0xf362,end=0xf3ee,parameters=0x1b6ca,channels=0xf350,guard=0xf3ee;
inline std::uint16_t word(const std::uint8_t* r,unsigned a) {return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a) {return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v) {r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v) {putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v));}
inline AudioSampleData readData(const std::uint8_t* r) {
    AudioSampleData d;
    for(unsigned i=0;i<8;++i) {const auto a=parameters+16*i; d.samples[i]={longword(r,a),longword(r,a+4),word(r,a+8),word(r,a+10),word(r,a+12)};}
    for(unsigned i=0;i<4;++i) d.channelMasks[i]=word(r,channels+4*i+2);
    return d;
}
inline void writeData(std::uint8_t* r,const AudioSampleData& d) {
    for(unsigned i=0;i<8;++i) {const auto a=parameters+16*i;const auto& s=d.samples[i];putLong(r,a,s.address);putLong(r,a+4,s.byteLength);putWord(r,a+8,s.period);putWord(r,a+10,s.volume);putWord(r,a+12,s.channel);}
    for(unsigned i=0;i<4;++i) putWord(r,channels+4*i+2,d.channelMasks[i]);
}
inline AudioPlaybackState readState(const std::uint8_t* r) {
    AudioPlaybackState s;for(unsigned i=0;i<4;++i)s.completedInterrupts[i]=word(r,channels+4*i);s.dmaEnableGuard=word(r,guard);return s;
}
inline void writeState(std::uint8_t* r,const AudioPlaybackState& s) {
    for(unsigned i=0;i<4;++i)putWord(r,channels+4*i,s.completedInterrupts[i]);putWord(r,guard,s.dmaEnableGuard);
}
} // namespace scr::test::audio

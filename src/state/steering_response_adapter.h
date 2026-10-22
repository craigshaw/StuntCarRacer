#ifndef SCR_STATE_STEERING_RESPONSE_ADAPTER_H
#define SCR_STATE_STEERING_RESPONSE_ADAPTER_H
#include "steering_response.h"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace scr::state::steering {
constexpr unsigned tableAddress=0x6125a,integrityAddress=0x64aec;
using Config=TrackSegmentConfiguration;
inline constexpr std::array<std::pair<unsigned,std::uint8_t Config::*>,15> configBytes{{
    {0x1bb79,&Config::format},{0x1bbdc,&Config::alternateFlag},{0x1bc4a,&Config::slopeFlags},
    {0x1bc32,&Config::reverseDirection},{0x1bb86,&Config::geometryType},{0x1bb4d,&Config::steeringFlags},
    {0x1bb97,&Config::limit},{0x1bb59,&Config::limitDoubled},{0x1bb98,&Config::maximum},{0x1bb5a,&Config::maximumDoubled},
    {0x1bb6a,&Config::mode},{0x1bc44,&Config::directionInversion},{0x1bb7b,&Config::primaryOrientation},
    {0x1bbd9,&Config::alternateOrientation},{0x1bbd4,&Config::steeringBase}}};
inline constexpr std::array<std::pair<unsigned,std::uint16_t Config::*>,5> configWords{{
    {0x1bc8c,&Config::primaryOffset},{0x1bc90,&Config::alternateOffset},{0x1bcbc,&Config::descriptorOffset},
    {0x1bc0e,&Config::primaryBase},{0x1bc10,&Config::alternateBase}}};
inline std::uint16_t word(const std::uint8_t* r,unsigned a){return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a){return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v){r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v){putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v));}
inline SteeringResponseInputs readInputs(const std::uint8_t* r) {
    return {word(r,0x1bd5a),word(r,0x1bd30),word(r,0x1bcf2),r[0x1bb1c],r[0x1bb0a],r[0x1bb7e],r[0x1bc33],longword(r,integrityAddress)};
}
inline void writeInputs(std::uint8_t* r,const SteeringResponseInputs& in) {
    putWord(r,0x1bd5a,in.targetAngle);putWord(r,0x1bd30,in.forwardVelocity);putWord(r,0x1bcf2,in.yawVelocity);
    r[0x1bb1c]=in.playerSegment;r[0x1bb0a]=in.trackProgression;r[0x1bb7e]=in.wheelMovementActive;r[0x1bc33]=in.reverseDirectionLowByte;
    putLong(r,integrityAddress,in.integrityWord);
}
inline SteeringResponseState readState(const std::uint8_t* r) {
    SteeringResponseState s;
    for(auto [a,member]:configBytes)s.segment.*member=r[a];for(auto [a,member]:configWords)s.segment.*member=word(r,a);
    s.currentSegment=r[0x1bb85];s.steeringDirection=r[0x1bbc6];s.directionMultiplier=r[0x1bb5d];s.scratch3=r[0x1bb1a];s.scratch4=r[0x1bb1b];
    s.yawAngle=word(r,0x1bce6);s.progressDistance=word(r,0x1bc2a);s.steeringScale=word(r,0x1bc3c);s.angleDeltaScratch=word(r,0x1bbf6);s.yawAcceleration=word(r,0x1bcfe);return s;
}
inline void writeState(std::uint8_t* r,const SteeringResponseState& s) {
    for(auto [a,member]:configBytes)r[a]=s.segment.*member;for(auto [a,member]:configWords)putWord(r,a,s.segment.*member);
    r[0x1bb85]=s.currentSegment;r[0x1bbc6]=s.steeringDirection;r[0x1bb5d]=s.directionMultiplier;r[0x1bb1a]=s.scratch3;r[0x1bb1b]=s.scratch4;
    putWord(r,0x1bce6,s.yawAngle);putWord(r,0x1bc2a,s.progressDistance);putWord(r,0x1bc3c,s.steeringScale);putWord(r,0x1bbf6,s.angleDeltaScratch);putWord(r,0x1bcfe,s.yawAcceleration);
}
inline SteeringOffsetTable readOffsets(const std::uint8_t* r){return {word(r,tableAddress),word(r,tableAddress+2),word(r,tableAddress+4)};}
inline void writeOffsets(std::uint8_t* r,const SteeringOffsetTable& t){for(unsigned i=0;i<3;++i)putWord(r,tableAddress+i*2,t[i]);}
} // namespace scr::state::steering
#endif

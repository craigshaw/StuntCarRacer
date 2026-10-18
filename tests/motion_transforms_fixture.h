#ifndef SCR_TEST_MOTION_TRANSFORMS_FIXTURE_H
#define SCR_TEST_MOTION_TRANSFORMS_FIXTURE_H
#include "motion_transforms.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace scr::test::motion {
constexpr unsigned selectorAddress=0x1ec46,coefficientAddress=0x1c230;
constexpr std::array<unsigned,4> entries{0x6158c,0x615e6,0x61618,0x61672};
constexpr std::array<unsigned,4> ends{0x615e6,0x61618,0x61672,0x616d6};
constexpr std::array<unsigned,9> inputWords{0x1bcea,0x1bcec,0x1bcee,0x1bd32,0x1bd34,0x1bd36,0x1bcf0,0x1bcf2,0x1bcf4};
constexpr std::array<unsigned,11> outputWords{0x1bd2c,0x1bd30,0x1bd0e,0x1bd10,0x1bd12,0x1bcf6,0x1bcf8,0x1bcfa,0x1bd3a,0x1bd3c,0x1bd3e};
constexpr std::array<std::array<unsigned,2>,4> observedRanges{{{0x1bcf6,0x1bcfc},{0x1bd0e,0x1bd14},{0x1bd2c,0x1bd32},{0x1bd3a,0x1bd40}}};
inline std::uint16_t readWord(const std::uint8_t* r,unsigned a) {return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline void writeWord(std::uint8_t* r,unsigned a,std::uint16_t v) {r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline MotionTransformInputs readInputs(const std::uint8_t* r) {
    MotionTransformInputs in;
    for(unsigned i=0;i<3;++i){in.worldSpeed[i]=readWord(r,0x1bcea+i*2);in.adjustedAngles[i]=readWord(r,0x1bd32+i*2);in.rotationSpeed[i]=readWord(r,0x1bcf0+i*2);}
    return in;
}
inline void writeInputs(std::uint8_t* r,const MotionTransformInputs& in) {
    for(unsigned i=0;i<3;++i){writeWord(r,0x1bcea+i*2,in.worldSpeed[i]);writeWord(r,0x1bd32+i*2,in.adjustedAngles[i]);writeWord(r,0x1bcf0+i*2,in.rotationSpeed[i]);}
}
inline TransformMatrices readMatrices(const std::uint8_t* r) {
    TransformMatrices m;for(unsigned i=0;i<36;++i)m.words[i]=readWord(r,coefficientAddress+i*2);return m;
}
inline void writeMatrices(std::uint8_t* r,const TransformMatrices& m) {
    for(unsigned i=0;i<36;++i)writeWord(r,coefficientAddress+i*2,m.words[i]);
}
inline MotionTransformSelectors readSelectors(const std::uint8_t* r) {
    MotionTransformSelectors s;std::copy_n(r+selectorAddress,s.size(),s.begin());return s;
}
inline void writeSelectors(std::uint8_t* r,const MotionTransformSelectors& s) {std::copy(s.begin(),s.end(),r+selectorAddress);}
inline MotionTransformState readState(const std::uint8_t* r) {
    MotionTransformState s;
    for(unsigned i=0;i<3;++i){s.localVelocity[i]=readWord(r,0x1bd2c+i*2);s.worldAcceleration[i]=readWord(r,0x1bcf6+i*2);s.transformedAngularVelocity[i]=readWord(r,0x1bd3a+i*2);}
    s.baseTargetRoll=readWord(r,0x1bd0e);s.baseRollAngle=readWord(r,0x1bd10);s.additionalYawOffset=readWord(r,0x1bd12);return s;
}
inline void writeState(std::uint8_t* r,const MotionTransformState& s) {
    for(unsigned i=0;i<3;++i){writeWord(r,0x1bd2c+i*2,s.localVelocity[i]);writeWord(r,0x1bcf6+i*2,s.worldAcceleration[i]);writeWord(r,0x1bd3a+i*2,s.transformedAngularVelocity[i]);}
    writeWord(r,0x1bd0e,s.baseTargetRoll);writeWord(r,0x1bd10,s.baseRollAngle);writeWord(r,0x1bd12,s.additionalYawOffset);
}
} // namespace scr::test::motion
#endif

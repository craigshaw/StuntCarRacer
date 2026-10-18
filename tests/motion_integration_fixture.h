#ifndef SCR_TEST_MOTION_INTEGRATION_FIXTURE_H
#define SCR_TEST_MOTION_INTEGRATION_FIXTURE_H

#include "motion_integration.h"
#include <array>
#include <cstdint>

namespace scr::test::integration {

struct Inputs {
    MotionVector worldAcceleration{},angularAcceleration{};
    MotionPositionInputs position{};
    MotionAngleLimits limits{};
};
struct Range { unsigned begin,end; };
constexpr std::array<Range,3> stateRanges{{{0x1bcd8,0x1bcf6},{0x1bbab,0x1bbac},{0x1bc42,0x1bc44}}};
constexpr std::array<Range,5> inputRanges{{{0x1bcf6,0x1bd02},{0x1bd3a,0x1bd40},
    {0x1bb75,0x1bb76},{0x1bb9a,0x1bb9b},{0x61ad4,0x61adc}}};
constexpr unsigned positionEntry=0x61950,positionEnd=0x61ad4;
constexpr unsigned worldEntry=0x61adc,worldEnd=0x61b26;
constexpr unsigned angularEntry=0x61b26,angularEnd=0x61b70;
constexpr unsigned continuousEntry=0x618c0,continuousEnd=0x618ce;

inline std::uint16_t word(const std::uint8_t* ram,unsigned address) {
    return std::uint16_t((unsigned(ram[address])<<8)|ram[address+1]);
}
inline std::uint32_t longword(const std::uint8_t* ram,unsigned address) {
    return (std::uint32_t(word(ram,address))<<16)|word(ram,address+2);
}
inline void putWord(std::uint8_t* ram,unsigned address,std::uint16_t value) {
    ram[address]=std::uint8_t(value>>8); ram[address+1]=std::uint8_t(value);
}
inline void putLong(std::uint8_t* ram,unsigned address,std::uint32_t value) {
    putWord(ram,address,std::uint16_t(value>>16)); putWord(ram,address+2,std::uint16_t(value));
}
inline Inputs readInputs(const std::uint8_t* ram) {
    Inputs result{};
    for(unsigned i=0;i<3;++i) {
        result.worldAcceleration[i]=word(ram,0x1bcf6+i*2);
        result.angularAcceleration[i]=word(ram,0x1bcfc+i*2);
        result.position.transformedAngularVelocity[i]=word(ram,0x1bd3a+i*2);
    }
    result.position.raceCompletionFlag=ram[0x1bb75]; result.position.renderingOrderMode=ram[0x1bb9a];
    for(unsigned i=0;i<4;++i) result.limits[i]=word(ram,0x61ad4+i*2);
    return result;
}
inline void writeInputs(std::uint8_t* ram,const Inputs& input) {
    for(unsigned i=0;i<3;++i) {
        putWord(ram,0x1bcf6+i*2,input.worldAcceleration[i]);
        putWord(ram,0x1bcfc+i*2,input.angularAcceleration[i]);
        putWord(ram,0x1bd3a+i*2,input.position.transformedAngularVelocity[i]);
    }
    ram[0x1bb75]=input.position.raceCompletionFlag; ram[0x1bb9a]=input.position.renderingOrderMode;
    for(unsigned i=0;i<4;++i) putWord(ram,0x61ad4+i*2,input.limits[i]);
}
inline MotionIntegrationState readState(const std::uint8_t* ram) {
    MotionIntegrationState result{};
    for(unsigned i=0;i<3;++i) {
        result.worldPosition[i]=longword(ram,0x1bcd8+i*4); result.angles[i]=word(ram,0x1bce4+i*2);
        result.worldVelocity[i]=word(ram,0x1bcea+i*2); result.rotationVelocity[i]=word(ram,0x1bcf0+i*2);
    }
    result.steepPitchFlag=ram[0x1bbab]; result.viewportCenterY=word(ram,0x1bc42); return result;
}
inline void writeState(std::uint8_t* ram,const MotionIntegrationState& state) {
    for(unsigned i=0;i<3;++i) {
        putLong(ram,0x1bcd8+i*4,state.worldPosition[i]); putWord(ram,0x1bce4+i*2,state.angles[i]);
        putWord(ram,0x1bcea+i*2,state.worldVelocity[i]); putWord(ram,0x1bcf0+i*2,state.rotationVelocity[i]);
    }
    ram[0x1bbab]=state.steepPitchFlag; putWord(ram,0x1bc42,state.viewportCenterY);
}

} // namespace scr::test::integration
#endif // SCR_TEST_MOTION_INTEGRATION_FIXTURE_H

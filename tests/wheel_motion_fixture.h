#pragma once
#include "wheel_motion.h"
#include <array>
#include <cstdint>

namespace scr::test::wheelMotion {
struct MemoryRange { unsigned begin, end; };
constexpr unsigned speedEntry=0x60fbe, speedEnd=0x61012;
constexpr unsigned dampingEntry=0x62138, dampingEnd=0x6217a;
constexpr std::array<MemoryRange,2> codeRanges{{{speedEntry,speedEnd},{dampingEntry,dampingEnd}}};
// Compare every written byte and nearby untouched words, including the enhanced
// reference's wheelSpeed address 0x1bc60 and angularAccelerationY at 0x1bcfe.
constexpr std::array<MemoryRange,3> observedRanges{{
    {0x1bc60,0x1bc66},{0x1bd5a,0x1bd60},{0x1bcfa,0x1bd04},
}};
constexpr std::array<MemoryRange,7> inputRanges{{
    {0x1bd30,0x1bd32},{0x1bb7e,0x1bb7f},{0x1bcf0,0x1bcf2},
    {0x1bcf4,0x1bcf6},{0x1bd26,0x1bd2a},{0x1bd36,0x1bd38},{0x1bc62,0x1bc64},
}};
constexpr std::array<unsigned,2> speedOutputs{0x1bd5c,0x1bc62};
constexpr std::array<unsigned,2> dampingOutputs{0x1bcfc,0x1bd00};
inline std::uint16_t readWord(const std::uint8_t* ram,unsigned a) {
    return static_cast<std::uint16_t>((unsigned(ram[a])<<8)|ram[a+1]);
}
inline void writeWord(std::uint8_t* ram,unsigned a,std::uint16_t v) {
    ram[a]=static_cast<std::uint8_t>(v>>8); ram[a+1]=static_cast<std::uint8_t>(v);
}
inline void writeLong(std::uint8_t* ram,unsigned a,std::uint32_t v) {
    writeWord(ram,a,static_cast<std::uint16_t>(v>>16)); writeWord(ram,a+2,static_cast<std::uint16_t>(v));
}
inline WheelSpeedInputs readSpeedInputs(const std::uint8_t* ram) {
    return {readWord(ram,0x1bd30),ram[0x1bb7e]};
}
inline void writeSpeedInputs(std::uint8_t* ram,const WheelSpeedInputs& input) {
    writeWord(ram,0x1bd30,input.carLocalVelocityZ); ram[0x1bb7e]=input.wheelMovementActive;
}
inline WheelSpeedState readSpeedState(const std::uint8_t* ram) {
    return {readWord(ram,0x1bd5c),readWord(ram,0x1bc62)};
}
inline void writeSpeedState(std::uint8_t* ram,const WheelSpeedState& state) {
    writeWord(ram,0x1bd5c,state.carSpeedMagnitude); writeWord(ram,0x1bc62,state.wheelSpeed);
}
inline VelocityDampingInputs readDampingInputs(const std::uint8_t* ram) {
    return {readWord(ram,0x1bcf0),readWord(ram,0x1bcf4),readWord(ram,0x1bd26),
            readWord(ram,0x1bd28),readWord(ram,0x1bd36),ram[0x1bb7e]};
}
inline void writeDampingInputs(std::uint8_t* ram,const VelocityDampingInputs& input) {
    writeWord(ram,0x1bcf0,input.rotationSpeedX); writeWord(ram,0x1bcf4,input.rotationSpeedZ);
    writeWord(ram,0x1bd26,input.targetPitchRate); writeWord(ram,0x1bd28,input.targetRollRate);
    writeWord(ram,0x1bd36,input.adjustedYawAngle); ram[0x1bb7e]=input.wheelMovementActive;
}
inline VelocityDampingState readDampingState(const std::uint8_t* ram) {
    return {readWord(ram,0x1bcfc),readWord(ram,0x1bd00)};
}
inline void writeDampingState(std::uint8_t* ram,const VelocityDampingState& state) {
    writeWord(ram,0x1bcfc,state.angularAccelerationX); writeWord(ram,0x1bd00,state.angularAccelerationZ);
}
} // namespace scr::test::wheelMotion

#pragma once
#include "car_dynamics.h"
#include <array>
#include <cstdint>

namespace scr::test::carDynamics {
struct Range { unsigned begin,end; };
constexpr std::array<unsigned,4> entries{0x620b8,0x6217a,0x621da,0x621f4};
constexpr std::array<unsigned,4> ends{0x62138,0x621da,0x621f4,0x622dc};
constexpr std::array<Range,3> observedRanges{{{0x1bbc0,0x1bbc4},{0x1bd28,0x1bd3a},{0x1bcf4,0x1bcfe}}};
constexpr std::array<unsigned,7> wordOutputs{0x1bd2a,0x1bd32,0x1bd34,0x1bd36,0x1bcf6,0x1bcf8,0x1bcfa};
constexpr unsigned byteOutput=0x1bbc1;
inline std::uint16_t word(const std::uint8_t* ram,unsigned a) {
    return static_cast<std::uint16_t>((unsigned(ram[a])<<8)|ram[a+1]);
}
inline void putWord(std::uint8_t* ram,unsigned a,std::uint16_t value) {
    ram[a]=static_cast<std::uint8_t>(value>>8); ram[a+1]=static_cast<std::uint8_t>(value);
}
inline void putLong(std::uint8_t* ram,unsigned a,std::uint32_t value) {
    putWord(ram,a,static_cast<std::uint16_t>(value>>16)); putWord(ram,a+2,static_cast<std::uint16_t>(value));
}
inline CarOrientationInputs readOrientationInputs(const std::uint8_t* ram) {
    return {word(ram,0x1bd10),word(ram,0x1bd42),word(ram,0x1bd44),word(ram,0x1bd12),
            word(ram,0x1bd0e),word(ram,0x1bd40),word(ram,0x1bd2c),word(ram,0x1bd30),ram[0x1bb7e]};
}
inline void writeOrientationInputs(std::uint8_t* ram,const CarOrientationInputs& value) {
    putWord(ram,0x1bd10,value.baseRollAngle); putWord(ram,0x1bd42,value.pitchSpringAngle);
    putWord(ram,0x1bd44,value.yawSpringAngle); putWord(ram,0x1bd12,value.additionalYawOffset);
    putWord(ram,0x1bd0e,value.baseTargetRoll); putWord(ram,0x1bd40,value.rollSpringAngle);
    putWord(ram,0x1bd2c,value.carLocalVelocityX); putWord(ram,0x1bd30,value.carLocalVelocityZ);
    ram[0x1bb7e]=value.wheelMovementActive;
}
inline RollTransitionInputs readRollInputs(const std::uint8_t* ram) {
    return {word(ram,0x1bd0e),word(ram,0x1bd40),word(ram,0x1bd2c),word(ram,0x1bd42),ram[0x1bb7e]};
}
inline void writeRollInputs(std::uint8_t* ram,const RollTransitionInputs& value) {
    putWord(ram,0x1bd0e,value.baseTargetRoll); putWord(ram,0x1bd40,value.rollSpringAngle);
    putWord(ram,0x1bd2c,value.carLocalVelocityX); putWord(ram,0x1bd42,value.pitchSpringAngle);
    ram[0x1bb7e]=value.wheelMovementActive;
}
inline RollTransitionState readRollState(const std::uint8_t* ram) { return {word(ram,0x1bd32),ram[0x1bbc1]}; }
inline void writeRollState(std::uint8_t* ram,const RollTransitionState& value) {
    putWord(ram,0x1bd32,value.adjustedRollAngle); ram[0x1bbc1]=value.rollTransitionFlag;
}
inline CarOrientationState readOrientationState(const std::uint8_t* ram) {
    return {word(ram,0x1bd2a),word(ram,0x1bd34),word(ram,0x1bd36),readRollState(ram)};
}
inline void writeOrientationState(std::uint8_t* ram,const CarOrientationState& value) {
    putWord(ram,0x1bd2a,value.gasOutputAccumulator); putWord(ram,0x1bd34,value.adjustedPitchAngle);
    putWord(ram,0x1bd36,value.adjustedYawAngle); writeRollState(ram,value.roll);
}
inline CarDragInputs readDragInputs(const std::uint8_t* ram) {
    CarDragInputs value{};
    for(unsigned i=0;i<3;++i) { value.localVelocity[i]=word(ram,0x1bd2c+i*2); value.worldVelocity[i]=word(ram,0x1bcea+i*2); }
    value.wheelMovementActive=ram[0x1bb7e]; value.collisionResponseHighByte=ram[0x1bd46];
    value.offTrackStateFlags=ram[0x1bb9c]; value.postWreckStateFlag=ram[0x1bca2]; value.raceStartTimer=ram[0x1bbdf];
    value.draftingTimer=ram[0x1bbc7]; value.opponentAheadFlag=ram[0x1bbb8]; return value;
}
inline void writeDragInputs(std::uint8_t* ram,const CarDragInputs& value) {
    for(unsigned i=0;i<3;++i) { putWord(ram,0x1bd2c+i*2,value.localVelocity[i]); putWord(ram,0x1bcea+i*2,value.worldVelocity[i]); }
    ram[0x1bb7e]=value.wheelMovementActive; ram[0x1bd46]=value.collisionResponseHighByte;
    ram[0x1bb9c]=value.offTrackStateFlags; ram[0x1bca2]=value.postWreckStateFlag; ram[0x1bbdf]=value.raceStartTimer;
    ram[0x1bbc7]=value.draftingTimer; ram[0x1bbb8]=value.opponentAheadFlag;
}
inline CarDragState readDragState(const std::uint8_t* ram) {
    return {{{word(ram,0x1bcf6),word(ram,0x1bcf8),word(ram,0x1bcfa)}}};
}
inline void writeDragState(std::uint8_t* ram,const CarDragState& value) {
    for(unsigned i=0;i<3;++i) putWord(ram,0x1bcf6+i*2,value.worldAcceleration[i]);
}
} // namespace scr::test::carDynamics

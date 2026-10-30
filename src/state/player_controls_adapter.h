#pragma once
#include "race/player_controls.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace scr::state::controls {
constexpr unsigned keyboardAddress=0xead6,keyMapAddress=0x60c88;
inline std::uint16_t word(const std::uint8_t* r,unsigned a) {return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a) {return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v){r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v){putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v));}
inline ControlKeyMap readKeys(const std::uint8_t* r) {
    ControlKeyMap keys{};std::copy_n(r+keyMapAddress,keys.size(),keys.begin());return keys;
}
inline PlayerControlsInputs readInputs(const std::uint8_t* r,ControllerHardware hardware) {
    PlayerControlsInputs in{};std::copy_n(r+keyboardAddress,in.keyboard.size(),in.keyboard.begin());in.hardware=hardware;
    in.localVelocityZ=word(r,0x1bd30);in.wheelMovementActive=r[0x1bb7e];in.raceStartTimer=r[0x1bbdf];
    in.postWreckStateFlag=r[0x1bca2];in.displayUpdateFlag=r[0x5d724];in.gasInputIntensity=r[0x1bafa];
    in.gasInputAccumulator=r[0x1bafb];in.fuelConsumptionRate=r[0x1bafe];in.frameThrottleFlag=r[0x1bbcd];in.maxBoostFuel=r[0x1bb95];return in;
}
inline void writeInputs(std::uint8_t* r,const PlayerControlsInputs& in) {
    std::copy(in.keyboard.begin(),in.keyboard.end(),r+keyboardAddress);putWord(r,0x1bd30,in.localVelocityZ);
    r[0x1bb7e]=in.wheelMovementActive;r[0x1bbdf]=in.raceStartTimer;r[0x1bca2]=in.postWreckStateFlag;
    r[0x5d724]=in.displayUpdateFlag;r[0x1bafa]=in.gasInputIntensity;r[0x1bafb]=in.gasInputAccumulator;
    r[0x1bafe]=in.fuelConsumptionRate;r[0x1bbcd]=in.frameThrottleFlag;r[0x1bb95]=in.maxBoostFuel;
}
inline PlayerControlsState readState(const std::uint8_t* r) {
    return {r[0x1bb47],r[0x1b88e],r[0x60c86],longword(r,0x625a2),r[0x625a6],r[0x1bbc6],r[0x1bb70],
        r[0x1bba8],r[0x1bb62],r[0x1ca20],r[0x1bb3d],word(r,0x1bd2a)};
}
inline void writeState(std::uint8_t* r,const PlayerControlsState& s) {
    r[0x1bb47]=s.inputFlags;r[0x1b88e]=s.joystickState;r[0x60c86]=s.cipherIndex;putLong(r,0x625a2,s.randomSeed);
    r[0x625a6]=s.randomTail;r[0x1bbc6]=s.steeringDirection;r[0x1bb70]=s.raceActive;r[0x1bba8]=s.accelerationState;
    r[0x1bb62]=s.boostActive;r[0x1ca20]=s.boostFuel;r[0x1bb3d]=s.fuelTimer;putWord(r,0x1bd2a,s.gasOutput);
}
} // namespace scr::state::controls

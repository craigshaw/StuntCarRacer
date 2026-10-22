#pragma once
#include "suspension_effects.h"
#include <array>
#include <cstdint>

namespace scr::state::effects {
constexpr unsigned seedAddress=0x625a2;
inline std::uint16_t word(const std::uint8_t* r,unsigned a) { return std::uint16_t((unsigned(r[a])<<8)|r[a+1]); }
inline std::uint32_t longword(const std::uint8_t* r,unsigned a) { return (std::uint32_t(word(r,a))<<16)|word(r,a+2); }
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v) { r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v); }
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v) { putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v)); }
inline SuspensionEffectsInputs readInputs(const std::uint8_t* r) {
    return {word(r,0x1bcd0),word(r,0x1bc60),word(r,0x1bc5c),r[0x1bbcd],r[0x57c3c],r[0x1bbe0],
            r[0x1bb1c],r[0x1bb1d],r[0x1ca22],r[0x1bb70],r[0x1bb7d],word(r,0x1bc3a)};
}
inline void writeInputs(std::uint8_t* r,const SuspensionEffectsInputs& i) {
    putWord(r,0x1bcd0,i.carHeight);putWord(r,0x1bc60,i.trackSurfaceHeight);putWord(r,0x1bc5c,i.lateralOffset);
    r[0x1bbcd]=i.frameThrottle;r[0x57c3c]=i.networkMode;r[0x1bbe0]=i.multiplayerSync;r[0x1bb1c]=i.playerSegment;
    r[0x1bb1d]=i.opponentSegment;r[0x1ca22]=i.selectedTrack;r[0x1bb70]=i.raceActive;r[0x1bb7d]=i.hardImpactCount;
    putWord(r,0x1bc3a,i.maxCompressionVelocity);
}
inline SuspensionEffectsState readState(const std::uint8_t* r) {
    SuspensionEffectsState s{}; auto& race=s.race;
    race.timer=r[0x1bbdf];race.complete=r[0x1bbc4];race.trackSide=r[0x1bbe1];race.roll=word(r,0x1bc00);
    race.carRollAngle=word(r,0x1bce8);race.targetPitchRate=word(r,0x1bd26);race.crashRecoveryTimer=r[0x1bb74];
    race.offTrackFlags=r[0x1bb9c];race.messageActive=r[0x1bb8e];race.messageIndex=r[0x5e65b];race.messageMode=r[0x5e65a];
    race.randomSeed=longword(r,seedAddress);race.randomTail=r[0x625a6];
    auto& collision=s.collision;collision.active=r[0x1bb46];collision.cooldown=r[0x63ee0];collision.opponentSpeed=word(r,0x1bbee);
    for(unsigned i=0;i<3;++i) {collision.wheelVelocities[i]=word(r,0x1bd76+i*2);collision.forces[i]=word(r,0x1bd54+i*2);s.springAngles[i]=word(r,0x1bd40+i*2);}
    s.savedYawSpring=word(r,0x1bd46);s.impactCooldown=r[0x620b6];s.impactVolume=r[0x1b705];return s;
}
inline void writeState(std::uint8_t* r,const SuspensionEffectsState& s) {
    const auto& race=s.race;
    r[0x1bbdf]=race.timer;r[0x1bbc4]=race.complete;r[0x1bbe1]=race.trackSide;putWord(r,0x1bc00,race.roll);
    putWord(r,0x1bce8,race.carRollAngle);putWord(r,0x1bd26,race.targetPitchRate);r[0x1bb74]=race.crashRecoveryTimer;
    r[0x1bb9c]=race.offTrackFlags;r[0x1bb8e]=race.messageActive;r[0x5e65b]=race.messageIndex;r[0x5e65a]=race.messageMode;
    putLong(r,seedAddress,race.randomSeed);r[0x625a6]=race.randomTail;
    const auto& collision=s.collision;r[0x1bb46]=collision.active;r[0x63ee0]=collision.cooldown;putWord(r,0x1bbee,collision.opponentSpeed);
    for(unsigned i=0;i<3;++i) {putWord(r,0x1bd76+i*2,collision.wheelVelocities[i]);putWord(r,0x1bd54+i*2,collision.forces[i]);putWord(r,0x1bd40+i*2,s.springAngles[i]);}
    putWord(r,0x1bd46,s.savedYawSpring);r[0x620b6]=s.impactCooldown;r[0x1b705]=s.impactVolume;
}
} // namespace scr::state::effects

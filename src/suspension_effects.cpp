#include "suspension_effects.h"

namespace scr {
namespace {
constexpr std::int32_t signedWord(std::uint16_t value) noexcept {
    return value<0x8000 ? std::int32_t(value) : std::int32_t(value)-65536;
}
constexpr std::int32_t floorDivide(std::int32_t value,std::int32_t divisor) noexcept {
    return value/divisor-(value%divisor<0 ? 1 : 0);
}

void checkMultiplayerSide(const SuspensionEffectsInputs& input,RaceStartState& race) noexcept {
    if(!input.networkMode || !(race.complete&0x80) || !(input.multiplayerSync&0x80)) return;
    if(input.playerSegment!=input.opponentSegment) return;
    if(((std::uint32_t(input.multiplayerSync)<<1)^race.trackSide)&0x80) return;
    race.trackSide^=0x80;
}

bool updateStartRoll(std::uint8_t direction,const SuspensionEffectsInputs& input,RaceStartState& race) noexcept {
    const bool opposite=(race.trackSide&0x80)!=0;
    const std::uint8_t target=opposite ? 0xf0 : 0x10;
    if(opposite) direction=static_cast<std::uint8_t>(0u-direction);
    const auto angle=static_cast<std::uint16_t>(std::uint32_t(direction)<<8);
    const auto delta=floorDivide(signedWord(angle)*238,256);
    if(static_cast<std::uint8_t>(race.roll>>8)!=target)
        race.roll=static_cast<std::uint16_t>(std::int32_t(race.roll)+delta);
    const auto offset=static_cast<std::uint16_t>(std::uint32_t(input.lateralOffset)<<5);
    race.carRollAngle=static_cast<std::uint16_t>(std::uint32_t(race.roll)-offset);
    race.targetPitchRate=0;
    return static_cast<std::uint8_t>(race.roll>>8)==target;
}

bool adjustHeight(unsigned level,const SuspensionEffectsInputs& input,SuspensionEffectsState& state) noexcept {
    const auto difference=static_cast<std::uint16_t>(std::uint32_t(input.carHeight)-input.trackSurfaceHeight-(level<<8));
    auto force=floorDivide(signedWord(difference),8)-256;
    if(force<-512) force=-512;
    state.springAngles[1]=static_cast<std::uint16_t>(std::int32_t(state.springAngles[1])-force);
    // The caller's BPL uses flags from ADDQ.B#2 to the difference's HIGH byte.
    const auto result=static_cast<std::uint8_t>((difference>>8)+2u);
    return (result&0x80)==0;
}

std::uint8_t advanceRandomSeed(RaceStartState& race) noexcept {
    const auto high=static_cast<std::uint16_t>(race.randomSeed>>16);
    const auto low=static_cast<std::uint16_t>(race.randomSeed);
    const auto result=static_cast<std::uint8_t>((low>>4)^(high>>1));
    race.randomSeed=(race.randomSeed<<8)|race.randomTail;
    race.randomTail=result;
    return result;
}
} // namespace

void updateRaceStartCountdown(const SuspensionEffectsInputs& inputs,SuspensionEffectsState& state) noexcept {
    auto& race=state.race;
    if(!race.timer) return;
    if(race.timer>=0xe6) {
        checkMultiplayerSide(inputs,race);
        race.roll=(race.trackSide&0x80) ? 0xd400 : 0x2c00;
        --race.timer; // Original path does not consult frameThrottle.
        return;
    }
    if(race.timer==0xe5) {
        updateStartRoll(0,inputs,race);
        if(adjustHeight(3,inputs,state)) --race.timer;
        return;
    }
    if(race.timer==0xe4) {
        adjustHeight(4,inputs,state);
        if(!updateStartRoll(0xff,inputs,race)) return;
        race.timer=static_cast<std::uint8_t>((advanceRandomSeed(race)&0x1f)+0xa0);
        race.messageIndex=(race.complete&0x80) ? 0x3c : 0x2c;
        if(!(inputs.selectedTrack&0x80)) race.timer=0x8c;
        if(race.crashRecoveryTimer) race.crashRecoveryTimer=0x32;
        race.messageActive=0x80; race.messageMode=4;
        return;
    }
    updateStartRoll(0,inputs,race);
    adjustHeight(2,inputs,state);
    if(!(inputs.frameThrottle&0x80)) {
        --race.timer;
        if(!race.timer) ++race.timer;
    }
    const bool start=race.complete ? inputs.raceActive==0 : (race.timer&0x80)==0;
    if(start) {
        race.timer=0; race.offTrackFlags=0; race.messageActive=0; race.complete=0x80;
    }
}

bool applyOpponentCollisionForces(SuspensionEffectsState& state) noexcept {
    auto& collision=state.collision;
    if(collision.cooldown) --collision.cooldown;
    if(!collision.active) return false;
    collision.active=0;
    auto speed=static_cast<std::uint16_t>(std::uint32_t(collision.opponentSpeed)-collision.forces[2]);
    if(speed&0x8000) speed=0;
    collision.opponentSpeed=speed;
    const auto vertical=floorDivide(signedWord(collision.forces[1]),16);
    for(auto& velocity:collision.wheelVelocities)
        velocity=static_cast<std::uint16_t>(std::int32_t(velocity)-vertical);
    for(unsigned axis=0;axis<3;++axis) {
        state.springAngles[axis]=static_cast<std::uint16_t>(std::uint32_t(state.springAngles[axis])+collision.forces[axis]);
        collision.forces[axis]=0;
    }
    if(collision.cooldown) return false;
    collision.cooldown=5;
    return true;
}

SuspensionSampleRequests runSuspensionEffects(const SuspensionEffectsInputs& inputs,SuspensionEffectsState& state) noexcept {
    SuspensionSampleRequests requests{};
    updateRaceStartCountdown(inputs,state);
    state.savedYawSpring=state.springAngles[2];
    if(applyOpponentCollisionForces(state)) requests.ids[requests.count++]=2;
    if(state.impactCooldown) --state.impactCooldown;
    if(inputs.hardImpactCount) {
        auto volume=static_cast<std::uint8_t>(inputs.maxCompressionVelocity>>8);
        if(volume<7) volume=7;
        volume=static_cast<std::uint8_t>(std::uint32_t(volume)<<2); // Original byte shift wraps.
        if(volume>=64) volume=64;
        state.impactVolume=volume;
        if(!state.impactCooldown) {
            requests.ids[requests.count++]=3;
            state.impactCooldown=5;
        }
    }
    return requests;
}
} // namespace scr

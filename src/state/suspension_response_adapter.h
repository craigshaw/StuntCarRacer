#ifndef SCR_STATE_SUSPENSION_RESPONSE_ADAPTER_H
#define SCR_STATE_SUSPENSION_RESPONSE_ADAPTER_H

// Big-endian adapters for the recovered state layout.
// Addresses are verified against the supplied payload; no CPU is executed.
#include "suspension_response.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace scr::state::suspension {
constexpr unsigned tableAddress=0x1eeca;
inline std::uint16_t readWord(const std::uint8_t* ram,unsigned a) {
    return std::uint16_t((unsigned(ram[a])<<8)|ram[a+1]);
}
inline std::uint32_t readLong(const std::uint8_t* ram,unsigned a) {
    return (std::uint32_t(readWord(ram,a))<<16)|readWord(ram,a+2);
}
inline void writeWord(std::uint8_t* ram,unsigned a,std::uint16_t v) {
    ram[a]=std::uint8_t(v>>8); ram[a+1]=std::uint8_t(v);
}
inline void writeLong(std::uint8_t* ram,unsigned a,std::uint32_t v) {
    writeWord(ram,a,std::uint16_t(v>>16)); writeWord(ram,a+2,std::uint16_t(v));
}
inline SuspensionResponseInputs readInputs(const std::uint8_t* ram) {
    SuspensionResponseInputs input{};
    for(unsigned i=0;i<3;++i) {
        input.wheelHeight[i]=readLong(ram,0x1bca4+i*4);
        input.expectedHeight[i]=readLong(ram,0x1bc94+i*4);
    }
    input.trackOffsetAdjustment=readLong(ram,0x1bca0);
    input.trackWidthMultiplier=ram[0x1bb01]; input.frameThrottleFlag=ram[0x1bbcd];
    input.damageFrameLimit=ram[0x63ce2]; input.raceStartTimer=ram[0x1bbdf];
    input.pitchAngle=readWord(ram,0x1bce4); input.trackId=ram[0x1ca33];
    input.rotationSpeedX=readWord(ram,0x1bcf0);
    return input;
}
inline void writeInputs(std::uint8_t* ram,const SuspensionResponseInputs& input) {
    for(unsigned i=0;i<3;++i) {
        writeLong(ram,0x1bca4+i*4,input.wheelHeight[i]);
        writeLong(ram,0x1bc94+i*4,input.expectedHeight[i]);
    }
    writeLong(ram,0x1bca0,input.trackOffsetAdjustment);
    ram[0x1bb01]=input.trackWidthMultiplier; ram[0x1bbcd]=input.frameThrottleFlag;
    ram[0x63ce2]=input.damageFrameLimit; ram[0x1bbdf]=input.raceStartTimer;
    writeWord(ram,0x1bce4,input.pitchAngle); ram[0x1ca33]=input.trackId;
    writeWord(ram,0x1bcf0,input.rotationSpeedX);
}
inline SuspensionAttenuationTable readAttenuation(const std::uint8_t* ram) {
    SuspensionAttenuationTable result{}; std::copy_n(ram+tableAddress,128,result.begin()); return result;
}
inline SuspensionResponseState readState(const std::uint8_t* ram) {
    SuspensionResponseState state{};
    for(unsigned i=0;i<3;++i) {
        auto& wheel=state.wheels[i];
        wheel.travel=readLong(ram,0x1bcb0+i*4); wheel.clampedTravel=readWord(ram,0x1bd14+i*2);
        wheel.previousTravel=readWord(ram,0x1bd1a+i*2); wheel.velocity=readWord(ram,0x1bd20+i*2);
        wheel.accumulatedForce=ram[0x1bb4f+i];
    }
    state.maxCompressionVelocity=readWord(ram,0x1bc3a); state.hardImpactCount=ram[0x1bb7d];
    state.highCompressionFrameCount=ram[0x1bb56]; state.damageAccumulationActive=ram[0x1bb54];
    state.frontAverageVelocity=readWord(ram,0x1bbf6); state.averageVelocity=readWord(ram,0x1bd38);
    state.targetPitchRate=readWord(ram,0x1bd26); state.targetRollRate=readWord(ram,0x1bd28);
    state.wheelMovementActive=ram[0x1bb7e];
    auto& s=state.springs;
    s.rollAngle=readWord(ram,0x1bd40); s.pitchAngle=readWord(ram,0x1bd42); s.yawAngle=readWord(ram,0x1bd44);
    s.retainedWord46=readWord(ram,0x1bd46); s.rollDisplacement=readWord(ram,0x1bd48);
    s.pitchReferenceZero=readWord(ram,0x1bd4a); s.pitchDisplacement=readWord(ram,0x1bd4c);
    s.rollComponent={ram[0x1bd4e],ram[0x1bd4f]}; s.pitchComponent={ram[0x1bd50],ram[0x1bd51]};
    s.yawComponent={ram[0x1bd52],ram[0x1bd53]};
    s.scratch={ram[0x1bb1a],ram[0x1bb1b],ram[0x1bb2b],ram[0x1bb2c],ram[0x1bb2d],ram[0x1bbbb]};
    return state;
}
inline void writeState(std::uint8_t* ram,const SuspensionResponseState& state) {
    for(unsigned i=0;i<3;++i) {
        const auto& wheel=state.wheels[i];
        writeLong(ram,0x1bcb0+i*4,wheel.travel); writeWord(ram,0x1bd14+i*2,wheel.clampedTravel);
        writeWord(ram,0x1bd1a+i*2,wheel.previousTravel); writeWord(ram,0x1bd20+i*2,wheel.velocity);
        ram[0x1bb4f+i]=wheel.accumulatedForce;
    }
    writeWord(ram,0x1bc3a,state.maxCompressionVelocity); ram[0x1bb7d]=state.hardImpactCount;
    ram[0x1bb56]=state.highCompressionFrameCount; ram[0x1bb54]=state.damageAccumulationActive;
    writeWord(ram,0x1bbf6,state.frontAverageVelocity); writeWord(ram,0x1bd38,state.averageVelocity);
    writeWord(ram,0x1bd26,state.targetPitchRate); writeWord(ram,0x1bd28,state.targetRollRate);
    ram[0x1bb7e]=state.wheelMovementActive;
    const auto& s=state.springs;
    writeWord(ram,0x1bd40,s.rollAngle); writeWord(ram,0x1bd42,s.pitchAngle); writeWord(ram,0x1bd44,s.yawAngle);
    writeWord(ram,0x1bd46,s.retainedWord46); writeWord(ram,0x1bd48,s.rollDisplacement);
    writeWord(ram,0x1bd4a,s.pitchReferenceZero); writeWord(ram,0x1bd4c,s.pitchDisplacement);
    ram[0x1bd4e]=s.rollComponent.value; ram[0x1bd4f]=s.rollComponent.retainedLowByte;
    ram[0x1bd50]=s.pitchComponent.value; ram[0x1bd51]=s.pitchComponent.retainedLowByte;
    ram[0x1bd52]=s.yawComponent.value; ram[0x1bd53]=s.yawComponent.retainedLowByte;
    ram[0x1bb1a]=s.scratch.multiplier; ram[0x1bb1b]=s.scratch.productLow;
    ram[0x1bb2b]=s.scratch.rawDisplacement; ram[0x1bb2c]=s.scratch.savedAttenuation;
    ram[0x1bb2d]=s.scratch.attenuation; ram[0x1bbbb]=s.scratch.signByte;
}
} // namespace scr::state::suspension
#endif

#pragma once
#include "physics_update.h"
#include "audio_playback_fixture.h"
#include "car_dynamics_fixture.h"
#include "motion_integration_fixture.h"
#include "motion_transforms_fixture.h"
#include "steering_response_fixture.h"
#include "suspension_effects_fixture.h"
#include "suspension_response_fixture.h"
#include "track_sampling_fixture.h"
#include "wheel_motion_fixture.h"

namespace scr::test::physics {
using Ram = scr_test::ChipRam;
inline PhysicsInputs readInputs(const Ram& r) {
    PhysicsInputs in;
    in.trackHeading=steering::word(r.data(),0x1bd5a); in.lateralOrigin=steering::word(r.data(),0x1bc5e);
    in.forwardOrigin=steering::word(r.data(),0x1bb10); in.carHeight=steering::word(r.data(),0x1bcd0);
    in.trackSurfaceHeight=steering::word(r.data(),0x1bc60); in.lateralOffset=steering::word(r.data(),0x1bc5c);
    in.trackOffsetAdjustment=steering::longword(r.data(),0x1bca0); in.integrityWord=steering::longword(r.data(),0x64aec);
    in.playerSegment=r[0x1bb1c];in.opponentSegment=r[0x1bb1d];in.clampByteAtFF=r[0xff];in.trackProgression=r[0x1bb0a];
    in.reverseDirectionLowByte=r[0x1bc33];in.trackWidthMultiplier=r[0x1bb01];in.damageFrameLimit=r[0x63ce2];in.trackId=r[0x1ca33];
    in.frameProcessingFlag=r[0x1bb72];in.frameThrottleFlag=r[0x1bbcd];in.networkMode=r[0x57c3c];in.multiplayerSync=r[0x1bbe0];
    in.selectedTrack=r[0x1ca22];in.raceActive=r[0x1bb70];in.raceCompletionFlag=r[0x1bb75];in.draftingTimer=r[0x1bbc7];in.opponentAhead=r[0x1bbb8];
    return in;
}
inline PhysicsData readData(const Ram& r) {
    PhysicsData d;
    d.track=scr_test::decodeTrackGeometry(r);
    for(unsigned i=0;i<d.trig.size();++i)d.trig[i]=steering::word(r.data(),0x1ca42+2*i);
    d.selectors=motion::readSelectors(r.data());d.attenuation=suspension::readAttenuation(r.data());
    d.steeringOffsets=steering::readOffsets(r.data());d.angleLimits=integration::readInputs(r.data()).limits;
    d.audio=audio::readData(r.data());return d;
}
inline PhysicsState readState(const Ram& r) {
    PhysicsState s;
    s.prefix.road.matrices=motion::readMatrices(r.data());
    s.prefix.road.wheelPositions=scr_test::decodeTrackSamplingInputs(r).wheels;
    s.prefix.road.track=scr_test::decodeTrackSamplingState(r);
    s.prefix.road.response=suspension::readState(r.data());
    s.prefix.motion=motion::readState(r.data());s.prefix.wheelSpeed=wheelMotion::readSpeedState(r.data());
    s.effects=effects::readState(r.data());s.orientation=carDynamics::readOrientationState(r.data());
    s.steering=steering::readState(r.data());s.motion=integration::readState(r.data());
    s.damping=wheelMotion::readDampingState(r.data());s.audio=audio::readState(r.data());return s;
}
inline void writeState(Ram& r,const PhysicsState& s) {
    motion::writeMatrices(r.data(),s.prefix.road.matrices);
    const auto& w=s.prefix.road.wheelPositions;
    const std::array<WheelPoint,3> points{w.frontLeft,w.frontRight,w.rearCenter};
    for(unsigned i=0;i<3;++i){steering::putWord(r.data(),0x1bd02+2*i,std::uint16_t(points[i].x));steering::putWord(r.data(),0x1bd08+2*i,std::uint16_t(points[i].y));}
    scr_test::encodeTrackSamplingState(r,s.prefix.road.track);
    const auto& h=s.prefix.road.expectedHeights;
    steering::putLong(r.data(),0x1bc94,std::uint32_t(h.frontLeft));steering::putLong(r.data(),0x1bc98,std::uint32_t(h.frontRight));steering::putLong(r.data(),0x1bc9c,std::uint32_t(h.rearCenter));
    steering::putWord(r.data(),0x1bbf6,h.pitchSine);
    suspension::writeState(r.data(),s.prefix.road.response);
    motion::writeState(r.data(),s.prefix.motion);wheelMotion::writeSpeedState(r.data(),s.prefix.wheelSpeed);
    effects::writeState(r.data(),s.effects);carDynamics::writeOrientationState(r.data(),s.orientation);
    steering::writeState(r.data(),s.steering);wheelMotion::writeDampingState(r.data(),s.damping);
    integration::writeState(r.data(),s.motion);audio::writeState(r.data(),s.audio);
}
} // namespace scr::test::physics

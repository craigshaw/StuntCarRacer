#pragma once
#include "race_restart.h"
#include "track_progress_adapter.h"
#include "physics_update_adapter.h"
#include "driving_effects_adapter.h"
#include <set>
namespace scr::state::restart {
using Ram=scr::state::memory::ChipRam;
using scr::state::memory::readWord;using scr::state::memory::readLong;using scr::state::memory::writeWord;using scr::state::memory::writeLong;
inline RaceRestartData readData(const Ram& r) {
    RaceRestartData d;d.track=scr::state::memory::decodeTrackGeometry(r);
    std::copy_n(r.begin()+0x1c588,100,d.segmentCoordinates.begin());
    std::copy_n(r.begin()+0x1f0c2,16,d.geometryRestartFlags.begin());
    std::copy_n(r.begin()+0x1c8e8,256,d.features.begin());d.featureCount=r[0x1ca2f];return d;
}
// Serializes the typed vehicle fields that the original clear loop visits.
// Address arithmetic is confined to this capture/test adapter.
template<class S,class W,class L> inline void vehicleFields(S& s,W word,L longword) {
    auto& v=s.vehicle;unsigned a=0x1bccc;
    const auto w=[&](auto& x){word(a,x);a+=2;};const auto l=[&](auto& x){longword(a,x);a+=4;};
    const auto array=[&](auto& xs){for(auto& x:xs)w(x);};
    l(v.rotatedX);w(v.carHeight);w(v.reservedHeight);l(v.rotatedZ);for(auto& x:v.worldPosition)l(x);
    array(v.angles);array(v.worldVelocity);array(v.rotationVelocity);array(v.worldAcceleration);array(v.angularAcceleration);
    array(v.wheelCoordinates);w(v.baseTargetRoll);w(v.baseRollAngle);w(v.additionalYawOffset);
    array(v.clampedSuspension);array(v.previousSuspension);array(v.suspensionVelocity);
    w(v.targetPitchRate);w(v.targetRollRate);w(v.gasOutput);array(v.localVelocity);array(v.adjustedAngles);
    w(v.averageWheelVelocity);array(v.transformedAngularVelocity);array(v.springAngles);w(v.reservedSpring);
    array(v.springDisplacements);array(v.springComponents);array(v.opponentCollisionForce);w(v.targetHeading);w(v.speedMagnitude);
    if(a!=0x1bd5e)throw std::logic_error("Restart vehicle adapter size mismatch");
}
inline RaceRestartState readState(const Ram& r) {
    RaceRestartState s;vehicleFields(s,[&](unsigned a,auto& v){v=readWord(r,a);},[&](unsigned a,auto& v){v=readLong(r,a);});
    s.segment=trackProgress::readState(r).segment;
    for(unsigned i=0;i<3;++i){s.wheelHeights[i]=readLong(r,0x1bca4+4*i);s.trackSurfaceHeights[i]=readLong(r,0x1bc94+4*i);}
    s.trackSurfaceHeight=readWord(r,0x1bc60);s.currentSegment=r[0x1bb85];s.playerSegment=r[0x1bb1c];
    s.gridLookupX=r[0x1bbd5];s.gridLookupZ=r[0x1bbd6];s.raceStartTimer=r[0x1bbdf];s.trackSide=r[0x1bbe1];
    s.trackSideCopy=r[0x1bbbb];s.multiplierScratch=r[0x1bb1a];s.chainVerticalPosition=r[0x1bbea];s.chainLiftVelocity=r[0x1bbe9];return s;
}
inline void writeState(Ram& r,const RaceRestartState& s) {
    vehicleFields(s,[&](unsigned a,const auto& v){writeWord(r,a,v);},[&](unsigned a,const auto& v){writeLong(r,a,v);});
    auto progress=trackProgress::readState(r);progress.segment=s.segment;trackProgress::writeState(r,progress);
    for(unsigned i=0;i<3;++i){writeLong(r,0x1bca4+4*i,s.wheelHeights[i]);writeLong(r,0x1bc94+4*i,s.trackSurfaceHeights[i]);}
    writeWord(r,0x1bc60,s.trackSurfaceHeight);r[0x1bb85]=s.currentSegment;r[0x1bb1c]=s.playerSegment;
    r[0x1bbd5]=s.gridLookupX;r[0x1bbd6]=s.gridLookupZ;r[0x1bbdf]=s.raceStartTimer;r[0x1bbe1]=s.trackSide;
    r[0x1bbbb]=s.trackSideCopy;r[0x1bb1a]=s.multiplierScratch;r[0x1bbea]=s.chainVerticalPosition;r[0x1bbe9]=s.chainLiftVelocity;
}
inline void prepare(Ram& r,const RaceRestartData& d,std::uint8_t requested) {auto s=readState(r);preparePlayerRestart(d,requested,s);writeState(r,s);}
inline void camera(Ram& r) {auto s=trackProgress::readState(r);updateTrackCamera(trackProgress::readInputs(r),s);trackProgress::writeState(r,s);}
inline void geometry(Ram& r,const TrackProgressData& d) {auto s=trackProgress::readState(r);applyTrackGeometry(d,trackProgress::readInputs(r),s);checkTrackBoundaries(s);trackProgress::writeState(r,s);}
inline void finish(Ram& r) {auto s=readState(r);finishPlayerRestart(r[0x1bbc4]!=0,motion::readMatrices(r.data()),s);writeState(r,s);}
inline void chain(Ram& r) {auto s=readState(r);finishRestartChain(s);writeState(r,s);}
inline void debris(Ram& r) {std::array<std::uint16_t,32> values{};initializeRestartDebris(values);for(unsigned i=0;i<values.size();++i)writeWord(r,0x1c3c0+2*i,values[i]);}
inline void wheels(Ram& r,const TrigTable& trig) {auto s=effects::readState(r);updateWheelVisuals(effects::readInputs(r),trig,s);effects::writeState(r,s);}
// Native initializer closure, sharing the recovered typed stages. The caller
// owns data and audio state; original and native RAM evolve independently.
inline void initialize(Ram& r,std::uint8_t requested,const RaceRestartData& data,const TrackProgressData& progress,PhysicsData& physicsData,std::vector<AudioRegisterWrite>& audioWrites) {
    prepare(r,data,requested);camera(r);geometry(r,progress);
    auto s=physics::readState(r);physicsData.audio=audio::readData(r.data());
    const auto writes=updateGamePhysics(physics::readInputs(r),physicsData,s);physics::writeState(r,s);
    audioWrites.insert(audioWrites.end(),writes.begin(),writes.end());finish(r);camera(r);chain(r);
}
} // namespace scr::state::restart

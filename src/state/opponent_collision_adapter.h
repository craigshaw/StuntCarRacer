#pragma once
#include "opponent_collision.h"
#include <algorithm>

namespace scr::state::contact {
inline std::uint16_t word(const std::uint8_t* r,unsigned a){return std::uint16_t((unsigned(r[a])<<8)|r[a+1]);}
inline std::uint32_t longword(const std::uint8_t* r,unsigned a){return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
inline void putWord(std::uint8_t* r,unsigned a,std::uint16_t v){r[a]=std::uint8_t(v>>8);r[a+1]=std::uint8_t(v);}
inline void putLong(std::uint8_t* r,unsigned a,std::uint32_t v){putWord(r,a,std::uint16_t(v>>16));putWord(r,a+2,std::uint16_t(v));}
inline PlayerDistanceData readDistanceData(const std::uint8_t* r){
    PlayerDistanceData d{};for(unsigned i=0;i<d.segmentWorldPositions.size();++i)d.segmentWorldPositions[i]=word(r,0x1c7e0+2*i);
    d.lapLength=word(r,0x1ca1e);return d;
}
inline void writeDistanceData(std::uint8_t* r,const PlayerDistanceData& d){
    for(unsigned i=0;i<d.segmentWorldPositions.size();++i)putWord(r,0x1c7e0+2*i,d.segmentWorldPositions[i]);putWord(r,0x1ca1e,d.lapLength);
}
inline PlayerDistanceInputs readDistanceInputs(const std::uint8_t* r){return {word(r,0x1bb0c),word(r,0x1bb0a),r[0x1bb1d],r[0x1bb1c]};}
inline void writeDistanceInputs(std::uint8_t* r,const PlayerDistanceInputs& i){putWord(r,0x1bb0c,i.opponentProgress);putWord(r,0x1bb0a,i.playerProgress);r[0x1bb1d]=i.opponentSegment;r[0x1bb1c]=i.playerSegment;}
inline PlayerDistanceState readDistanceState(const std::uint8_t* r){return {word(r,0x1bc3e),word(r,0x1bc38),r[0x1bbb8]};}
inline void writeDistanceState(std::uint8_t* r,const PlayerDistanceState& s){putWord(r,0x1bc3e,s.signedDifference);putWord(r,0x1bc38,s.distance);r[0x1bbb8]=s.opponentAhead;}
inline OpponentContactData readData(const std::uint8_t* r){
    OpponentContactData d{};d.segmentCount=r[0x1ca1a];
    std::copy_n(r+0x1c5ec,d.properties.size(),d.properties.begin());std::copy_n(r+0x1ed8a,d.behaviorTraits.size(),d.behaviorTraits.begin());
    std::copy_n(r+0x1f0c2,d.geometryFlags.size(),d.geometryFlags.begin());return d;
}
inline void writeData(std::uint8_t* r,const OpponentContactData& d){
    r[0x1ca1a]=d.segmentCount;std::copy(d.properties.begin(),d.properties.end(),r+0x1c5ec);
    std::copy(d.behaviorTraits.begin(),d.behaviorTraits.end(),r+0x1ed8a);std::copy(d.geometryFlags.begin(),d.geometryFlags.end(),r+0x1f0c2);
}
inline OpponentContactInputs readInputs(const std::uint8_t* r){
    return {r[0x1ca29],r[0x1bba1],word(r,0x1bc38),r[0x1bbb8],r[0x57c3c],r[0x1bbec],word(r,0x1bc5e),
        r[0x1bbbd],r[0x1bb44],r[0x1bbc4],r[0x1bb7e],r[0x1bb74],word(r,0x1bcd0),word(r,0x1bd66),word(r,0x1bbee),word(r,0x1bd30),r[0x1bb1d]};
}
inline void writeInputs(std::uint8_t* r,const OpponentContactInputs& i){
    r[0x1ca29]=i.opponentId;r[0x1bba1]=i.playerLateral;putWord(r,0x1bc38,i.opponentDistance);r[0x1bbb8]=i.opponentAhead;
    r[0x57c3c]=i.networkMode;r[0x1bbec]=i.distanceOffset;putWord(r,0x1bc5e,i.lateralRoadPosition);r[0x1bbbd]=i.movementOverride;
    r[0x1bb44]=i.aiEnabled;r[0x1bbc4]=i.raceStartComplete;r[0x1bb7e]=i.wheelMovementActive;r[0x1bb74]=i.crashRecoveryTimer;
    putWord(r,0x1bcd0,i.carHeight);putWord(r,0x1bd66,i.opponentHeight);putWord(r,0x1bbee,i.opponentSpeed);putWord(r,0x1bd30,i.playerForwardSpeed);r[0x1bb1d]=i.opponentSegment;
}
inline OpponentContactState readState(const std::uint8_t* r){
    return {r[0x1bbed],r[0x1bba7],r[0x1bbf7],r[0x1bbf6],r[0x1bbc7],r[0x1bbc3],r[0x1bbeb],r[0x1bb46],
        {word(r,0x1bd54),word(r,0x1bd56),word(r,0x1bd58)},{r[0x1bb4f],r[0x1bb50],r[0x1bb51]},r[0x1bb54],r[0x1bb19],r[0x1bb85]};
}
inline void writeState(std::uint8_t* r,const OpponentContactState& s){
    r[0x1bbed]=s.opponentLateral;r[0x1bba7]=s.targetLateral;r[0x1bbf7]=s.lateralDistance;r[0x1bbf6]=s.lateralDirection;
    r[0x1bbc7]=s.draftingTimer;r[0x1bbc3]=s.collisionTimer;r[0x1bbeb]=s.collisionLatched;r[0x1bb46]=s.collisionActive;
    for(unsigned i=0;i<3;++i){putWord(r,0x1bd54+i*2,s.forces[i]);r[0x1bb4f+i]=s.accumulatedDamage[i];}
    r[0x1bb54]=s.damageActive;r[0x1bb19]=s.lookaheadRemaining;r[0x1bb85]=s.currentSegment;
}
} // namespace scr::state::contact

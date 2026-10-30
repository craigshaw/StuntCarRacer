#include "race/opponent_collision.h"
#include <stdexcept>

namespace scr {
namespace {
bool negative(std::uint16_t value) noexcept { return (value&0x8000u)!=0; }
std::uint16_t negate(std::uint16_t value) noexcept { return std::uint16_t(0u-value); }
std::uint16_t magnitude(std::uint16_t value) noexcept { return negative(value)?negate(value):value; }
void aggressiveTarget(OpponentContactState& state) noexcept {
    if(state.lateralDistance<0x38)state.targetLateral=(state.lateralDirection&0x80)?0x20:0xe0;
}
void standardTarget(const OpponentContactInputs& inputs,OpponentContactState& state) noexcept {
    if(state.lateralDistance>=0x38)return;
    const bool right=(state.lateralDirection&0x80)?inputs.playerLateral>=0x60:inputs.playerLateral>=0xa0;
    state.targetLateral=right?0x20:0xe0;
}
void moveLaterally(const OpponentContactInputs& inputs,OpponentContactState& state) noexcept {
    auto difference=inputs.movementOverride;
    bool left=(difference&0x80)!=0;
    if(difference==0){
        difference=std::uint8_t(state.targetLateral-state.opponentLateral);
        if(difference==0)return;
        left=state.targetLateral<state.opponentLateral;
    }
    if(left?difference>=0xf0:difference<0x10)return;
    const auto next=std::uint8_t(state.opponentLateral+(left?-9:9));
    if(inputs.aiEnabled&&next>=0x20&&next<0xe1&&!inputs.networkMode)state.opponentLateral=next;
}
}

PlayerDistanceState calculatePlayerDistance(const PlayerDistanceData& data,const PlayerDistanceInputs& inputs) {
    if(inputs.opponentSegment>=data.segmentWorldPositions.size()||inputs.playerSegment>=data.segmentWorldPositions.size())
        throw std::invalid_argument("Player distance segment index outside recovered domain");
    const auto progressDifference=std::uint16_t(inputs.opponentProgress-inputs.playerProgress);
    const auto scaled=std::uint16_t((progressDifference>>3)|(negative(progressDifference)?0xe000u:0u));
    const auto difference=std::uint16_t(scaled+data.segmentWorldPositions[inputs.opponentSegment]-data.segmentWorldPositions[inputs.playerSegment]);
    const auto direct=magnitude(difference),around=std::uint16_t(data.lapLength-direct);
    const bool wrap=around<direct;
    return {difference,wrap?around:direct,std::uint8_t((difference^(wrap?0x8000u:0u))>>8)};
}

void calculateOpponentCollisionForces(const OpponentContactInputs& inputs,OpponentContactState& state) noexcept {
    if(!inputs.raceStartComplete)return;
    if(!inputs.aiEnabled||!inputs.wheelMovementActive){
        const auto heightDifference=std::uint16_t(inputs.carHeight-inputs.opponentHeight);
        const auto separation=magnitude(std::uint16_t(heightDifference+0x28u));
        // Signed BGE after NEG.W: the unrepresentable magnitude 0x8000
        // deliberately passes this test, just as on the original 68000.
        if(!negative(separation)&&separation>=0xc0){state.collisionTimer=3;return;}
        if(state.collisionTimer){
            --state.collisionTimer;
            auto impulse=std::uint16_t(0x100u-separation);
            if(negative(heightDifference))impulse=negate(impulse);
            state.forces[1]=std::uint16_t(unsigned(impulse)<<4);
        }
    }
    if(state.lateralDistance<0x2d&&std::uint8_t(inputs.opponentDistance)<8){
        const unsigned high=(state.lateralDirection&0x80)?0x0800u:0xf800u;
        state.forces[0]=std::uint16_t(high|(state.forces[0]&0xffu));
    }
    if(!(state.collisionLatched&0x80)){
        const auto difference=std::uint16_t((inputs.crashRecoveryTimer?0:inputs.opponentSpeed)-inputs.playerForwardSpeed);
        const auto half=std::uint16_t((difference>>1)|(difference&0x8000u));
        state.forces[2]=std::uint16_t(half+(negative(difference)?-3:3));
    }
    state.collisionActive=state.collisionLatched=0x80;
    auto total=std::uint16_t(0x200);
    for(auto force:state.forces)total=std::uint16_t(total+magnitude(force));
    const unsigned damage=total>>8;
    for(auto& value:state.accumulatedDamage){const unsigned sum=value+damage;value=std::uint8_t(sum>255?255:sum);}
    state.damageActive=0x80;
}

void handleOpponentPositioning(const OpponentContactData& data,const OpponentContactInputs& inputs,OpponentContactState& state) {
    if(inputs.opponentId>=data.behaviorTraits.size()||data.segmentCount==0||data.segmentCount>data.properties.size()||inputs.opponentSegment>=data.segmentCount)
        throw std::invalid_argument("Opponent positioning table index outside recovered domain");
    state.draftingTimer=0;state.targetLateral=state.opponentLateral;
    const bool left=state.opponentLateral<inputs.playerLateral;
    state.lateralDistance=std::uint8_t(left?inputs.playerLateral-state.opponentLateral:state.opponentLateral-inputs.playerLateral);
    state.lateralDirection=left?0xff:0;
    const auto traits=data.behaviorTraits[inputs.opponentId];
    bool far=inputs.opponentDistance>=0x100,scan=true;
    if(!far){
        const auto distance=std::uint8_t(inputs.opponentDistance);
        if(distance<0x40&&((inputs.opponentAhead&0x80)||state.lateralDistance<0x32))state.draftingTimer=0xff;
        const bool contact=distance<0x10&&(!inputs.networkMode||!inputs.distanceOffset)&&state.lateralDistance<0x32&&inputs.lateralRoadPosition<0x180;
        if(contact)calculateOpponentCollisionForces(inputs,state);
        else {state.collisionTimer=0;state.collisionLatched=0;}
        if(contact||distance<0x18){
            far=(traits&8)&&!(inputs.opponentAhead&0x80)&&distance>=0x0e;
            if(!far){aggressiveTarget(state);scan=false;}
        }else if(inputs.opponentAhead&0x80){standardTarget(inputs,state);scan=false;}
        else if(distance<0x32){
            if(traits&2)state.targetLateral=inputs.playerLateral;else standardTarget(inputs,state);
        }else if(distance<0xc8&&(traits&0x20))standardTarget(inputs,state);
        else far=true;
    }
    if(far){
        state.targetLateral=(traits&8)?0x6e:0x40;
        if(inputs.opponentId&1)state.targetLateral=std::uint8_t(~state.targetLateral);
    }
    if(scan){
        state.lookaheadRemaining=2;state.currentSegment=inputs.opponentSegment;
        do{
            if(data.geometryFlags[data.properties[state.currentSegment]&15]&0x80)state.targetLateral=0x80;
            state.currentSegment=std::uint8_t(state.currentSegment+1);
            if(state.currentSegment>=data.segmentCount)state.currentSegment=0;
        }while(--state.lookaheadRemaining);
    }
    moveLaterally(inputs,state);
}
} // namespace scr

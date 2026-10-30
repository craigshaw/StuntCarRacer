#ifndef SCR_OPPONENT_COLLISION_H
#define SCR_OPPONENT_COLLISION_H
#include <array>
#include <cstdint>

namespace scr {
struct PlayerDistanceData {
    std::array<std::uint16_t,100> segmentWorldPositions{};
    std::uint16_t lapLength{};
};
struct PlayerDistanceInputs {
    std::uint16_t opponentProgress{},playerProgress{};
    std::uint8_t opponentSegment{},playerSegment{};
};
struct PlayerDistanceState {
    std::uint16_t signedDifference{},distance{};
    std::uint8_t opponentAhead{};
};
// Complete original [6076c,607d0). Segment indices must be below100;
// original D1/D2 bits8..15 must be zero. The direction output preserves the
// entire high byte of the signed difference, including its original sign flip.
[[nodiscard]] PlayerDistanceState calculatePlayerDistance(const PlayerDistanceData& data,const PlayerDistanceInputs& inputs);

struct OpponentContactData {
    std::uint8_t segmentCount{};
    std::array<std::uint8_t,100> properties{};
    std::array<std::uint8_t,32> behaviorTraits{};
    std::array<std::uint8_t,16> geometryFlags{};
};
struct OpponentContactInputs {
    std::uint8_t opponentId{},playerLateral{};
    std::uint16_t opponentDistance{};
    std::uint8_t opponentAhead{},networkMode{},distanceOffset{};
    std::uint16_t lateralRoadPosition{};
    std::uint8_t movementOverride{},aiEnabled{},raceStartComplete{},wheelMovementActive{},crashRecoveryTimer{};
    std::uint16_t carHeight{},opponentHeight{},opponentSpeed{},playerForwardSpeed{};
    std::uint8_t opponentSegment{};
};
struct OpponentContactState {
    std::uint8_t opponentLateral{},targetLateral{},lateralDistance{},lateralDirection{},draftingTimer{};
    std::uint8_t collisionTimer{},collisionLatched{},collisionActive{};
    std::array<std::uint16_t,3> forces{}; // lateral, vertical, forward
    std::array<std::uint8_t,3> accumulatedDamage{};
    std::uint8_t damageActive{},lookaheadRemaining{},currentSegment{};
};

// Complete original [63ce4,63e2e). Retains forces on paths which do not
// overwrite them, including the LOW byte of the lateral impulse.
void calculateOpponentCollisionForces(const OpponentContactInputs& inputs,OpponentContactState& state) noexcept;

// Complete original [636c0,63996), including two-segment lookahead and the
// collision producer. Requires opponentId<32, segmentCount in [1,100], and
// opponentSegment<segmentCount. Original D1/D2 bits8..15 must be zero at entry.
// Opponent progress, speed, height and distance are supplied by earlier stages;
// this stage does not simulate the opponent's driving or wheel geometry.
void handleOpponentPositioning(const OpponentContactData& data,const OpponentContactInputs& inputs,OpponentContactState& state);
} // namespace scr
#endif

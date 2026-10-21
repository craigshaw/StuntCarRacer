#pragma once
#include "replay_interpolation.h"
#include <cstdint>

namespace scr::viewer {
struct DriveKeys {bool accelerate{},brake{},left{},right{},boost{};};
struct DrivePad {
    bool connected{};
    std::int16_t x{},y{};
    std::uint8_t leftTrigger{},rightTrigger{};
    std::uint16_t buttons{};
};
// Original digital control mask: gas1,brake2,left4,right8,boost16.
enum class DriveControlPreset { Original, Modern };
// The development viewer retains its combined stick/trigger mapping.
unsigned mapDriveControls(const DriveKeys&,const DrivePad&) noexcept;
unsigned mapDriveControls(const DriveKeys&,const DrivePad&,DriveControlPreset) noexcept;
unsigned resolveDriveControls(unsigned mask) noexcept;
class DriveInputLatch {
public:
    void sample(unsigned mask) noexcept {held_=mask&31;pending_|=held_;}
    unsigned consume(DriveControlPreset preset=DriveControlPreset::Original) noexcept {
        // Modern gas follows the current held state. Retain short pulses for
        // other actions, but never replay gas after the trigger was released.
        const auto pending=pending_&(preset==DriveControlPreset::Modern?30u:31u);
        const auto result=resolveDriveControls(held_|pending);pending_=0;return result;
    }
    void clear() noexcept {held_=pending_=0;}
private:unsigned held_{},pending_{};
};
class FixedDriveClock {
public:
    // Long stalls admit at most8 ordinary ticks. No enlarged physics timestep.
    unsigned advance(double elapsed);
    double fraction() const noexcept {return accumulator_/replayTickSeconds;}
    void reset() noexcept {accumulator_=0;}
private:double accumulator_{};
};
} // namespace scr::viewer

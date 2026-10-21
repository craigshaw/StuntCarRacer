#include "live_drive.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
unsigned resolveDriveControls(unsigned mask) noexcept {
    mask&=31;if(mask&2)mask&=~1u;if((mask&12)==12)mask&=~12u;return mask;
}
namespace {
unsigned mapControls(const DriveKeys& keys,const DrivePad& pad,bool stickThrottle,bool triggerThrottle) noexcept {
    bool gas=keys.accelerate,brake=keys.brake,left=keys.left,right=keys.right,boost=keys.boost;
    if(pad.connected){
        gas=gas||(triggerThrottle&&pad.rightTrigger>30)||(stickThrottle&&(pad.y>7849||(pad.buttons&1)));
        brake=brake||(triggerThrottle&&pad.leftTrigger>30)||(stickThrottle&&(pad.y<-7849||(pad.buttons&2)));
        left=left||pad.x<-7849||(pad.buttons&4);right=right||pad.x>7849||(pad.buttons&8);
        boost=boost||(pad.buttons&0x1200);
    }
    return resolveDriveControls((gas?1u:0u)|(brake?2u:0u)|(left?4u:0u)|(right?8u:0u)|(boost?16u:0u));
}
}
unsigned mapDriveControls(const DriveKeys& keys,const DrivePad& pad) noexcept {
    return mapControls(keys,pad,true,true);
}
unsigned mapDriveControls(const DriveKeys& keys,const DrivePad& pad,DriveControlPreset preset) noexcept {
    return mapControls(keys,pad,preset==DriveControlPreset::Original,preset==DriveControlPreset::Modern);
}
unsigned FixedDriveClock::advance(double elapsed){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Live elapsed time must be finite and nonnegative");
    accumulator_+=std::min(elapsed,8*replayTickSeconds);
    const auto steps=unsigned(std::floor((accumulator_+1e-12)/replayTickSeconds));
    accumulator_=std::max(0.0,accumulator_-steps*replayTickSeconds);return steps;
}
} // namespace scr::viewer

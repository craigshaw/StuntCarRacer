#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include "platform/windows/drive_input.h"

namespace scr::runtime {
PolledDrivePad WindowsDriveInput::poll(){
    PolledDrivePad result;XINPUT_STATE state{};const auto now=std::chrono::steady_clock::now();
    if(slot_<4&&XInputGetState(slot_,&state)!=ERROR_SUCCESS){
        slot_=4;previousButtons_=0;result.disconnected=true;nextSearch_=now+std::chrono::seconds(1);
    }
    if(slot_==4&&now>=nextSearch_){
        nextSearch_=now+std::chrono::seconds(1);
        for(unsigned n=0;n<4;++n)if(XInputGetState(n,&state)==ERROR_SUCCESS){slot_=n;previousButtons_=state.Gamepad.wButtons;break;}
    }
    if(slot_==4)return result;
    const auto& pad=state.Gamepad;result.pad={true,pad.sThumbLX,pad.sThumbLY,pad.bLeftTrigger,pad.bRightTrigger,pad.wButtons};
    const auto pressed=std::uint16_t(pad.wButtons&~previousButtons_);
    result.pausePressed=(pressed&XINPUT_GAMEPAD_START)!=0;result.resetPressed=(pressed&XINPUT_GAMEPAD_BACK)!=0;
    previousButtons_=pad.wButtons;return result;
}
} // namespace scr::runtime

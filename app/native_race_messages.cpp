#include "native_race_messages.h"
#include "native_menu_render.h"

namespace scr::viewer {
namespace {
unsigned word(const NativeMemory& m,unsigned address) {
    return (unsigned(m[address])<<8)|m[address+1];
}
}
bool NativeRaceMessageFrame::visible()const noexcept {
    // Original 0x5e508..0x5e540 uses signed byte branches. Countdown values
    // 1, 2 and 0x80..0xff suppress all messages except PRESS FIRE (0x3c).
    return (active&0x80)&&!(pause&0x80)&&
        (countdown==0||(countdown>=3&&countdown<0x80)||index==0x3c);
}
NativeRaceMessageFrame captureNativeRaceMessage(const NativeMemory& m) {
    NativeRaceMessageFrame result;
    result.active=m[0x1bb8e];result.countdown=m[0x1bb6c];result.pause=m[0x1bba4];
    result.blink=m[0x1bb88];result.index=m[0x5e65b];result.mode=m[0x5e65a];
    constexpr unsigned parameters=0x6a16c+51*16;
    result.panel.x=std::uint16_t((word(m,parameters+8)&255)*16);
    result.panel.y=std::uint16_t(word(m,parameters+10)&255);
    result.panel.widthWords=std::uint16_t(word(m,parameters+4)+1);
    result.panel.height=std::uint16_t(word(m,parameters+6)+1);
    return result;
}
void drawNativeRaceMessage(const NativeMemory& memory,const ReplayRenderAssets& assets,
        const NativeRaceMessageFrame& frame,std::vector<std::uint32_t>& pixels,bool restorePanel) {
    if(!frame.visible())return;
    if(restorePanel)compositeMaskedObject(assets,51,frame.panel,pixels);
    drawNativeGameMessage(memory,pixels,assets.palette,frame.index,frame.mode,frame.blink!=0);
}
} // namespace scr::viewer

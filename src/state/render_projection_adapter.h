#pragma once
#include "render/render_projection.h"
#include "race_timing_adapter.h"
#include <array>
namespace scr::state::renderProjection {
using timing::word;using timing::longword;using timing::putWord;using timing::putLong;
inline RenderProjectionTables readTables(const std::uint8_t* r){RenderProjectionTables t{};
    for(unsigned n=0;n<2048;++n){t.angleTable[n]=word(r,0x1cc46+2*n);t.magnitudeTable[n]=word(r,0x1dc46+2*n);}return t;
}
inline TrigTable readTrig(const std::uint8_t* r){TrigTable t{};for(unsigned n=0;n<t.size();++n)t[n]=word(r,0x1ca42+2*n);return t;}
inline RenderCameraInputs readCamera(const std::uint8_t* r){return {{longword(r,0x1bcd8),longword(r,0x1bcdc),longword(r,0x1bce0)},
    word(r,0x1bce4),word(r,0x1bce6),word(r,0x1bce8),word(r,0x1bd38)};}
inline void writeCamera(std::uint8_t* r,const RenderCameraInputs& in){
    for(unsigned n=0;n<3;++n)putLong(r,0x1bcd8+4*n,in.worldPosition[n]);
    putWord(r,0x1bce4,in.pitch);putWord(r,0x1bce6,in.yaw);putWord(r,0x1bce8,in.roll);putWord(r,0x1bd38,in.averageWheelVelocity);
}
inline int signedWord(std::uint16_t v){return v<0x8000?int(v):int(v)-65536;}
} // namespace scr::state::renderProjection

#pragma once
#include "opponent_car.h"
#include "render_projection_fixture.h"
namespace scr::test::opponentCar {
using renderProjection::word;using renderProjection::longword;using renderProjection::putWord;using renderProjection::putLong;
inline OpponentCarInputs readInputs(const std::uint8_t* r){
    OpponentCarInputs in{};constexpr unsigned coordinates[]{0xa,0x10,0xe,0x14};
    for(unsigned n=0;n<4;++n){in.footprint[n]=word(r,0x1bf64+2*n);in.relativeX[n]=word(r,0x1bd8e+coordinates[n]);in.relativeZ[n]=word(r,0x1bdae+coordinates[n]);}
    for(unsigned n=0;n<3;++n)in.bounds[n]=word(r,0x1bd86+2*n);
    in.camera.heightBaseline=word(r,0x1bbfa);in.camera.yawResidual=word(r,0x1bc2e);in.camera.viewportCenterY=word(r,0x1bc42);
    in.camera.sinRoll=word(r,0x1c250);in.camera.cosRoll=word(r,0x1c252);in.camera.quarterTurn=word(r,0x1bc30);
    in.distance=word(r,0x1bc38);in.roadOffset=word(r,0x1bbec);in.aheadFlag=r[0x1bbb8];in.curveSmoothing=r[0x1bbba];in.scaledPerspective=r[0x1bb8f];
    const auto line=longword(r,0x68144);in.lineBytes=line>=0x3d80?std::uint16_t(line-0x3d80):0;return in;
}
inline void writeInputs(std::uint8_t* r,const OpponentCarInputs& in){
    constexpr unsigned coordinates[]{0xa,0x10,0xe,0x14};
    for(unsigned n=0;n<4;++n){putWord(r,0x1bf64+2*n,in.footprint[n]);putWord(r,0x1bd8e+coordinates[n],in.relativeX[n]);putWord(r,0x1bdae+coordinates[n],in.relativeZ[n]);}
    for(unsigned n=0;n<3;++n)putWord(r,0x1bd86+2*n,in.bounds[n]);
    putWord(r,0x1bbfa,in.camera.heightBaseline);putWord(r,0x1bc2e,in.camera.yawResidual);putWord(r,0x1bc42,in.camera.viewportCenterY);
    putWord(r,0x1c250,in.camera.sinRoll);putWord(r,0x1c252,in.camera.cosRoll);putWord(r,0x1bc30,in.camera.quarterTurn);
    putWord(r,0x1bc38,in.distance);putWord(r,0x1bbec,in.roadOffset);r[0x1bbb8]=in.aheadFlag;r[0x1bbba]=in.curveSmoothing;r[0x1bb8f]=in.scaledPerspective;
    putLong(r,0x68144,0x3d80+in.lineBytes);putWord(r,0x68148,32);
    for(unsigned n=0;n<36;++n)putLong(r,0x7b08a+0x5e0+n*4,0x80000000);
}
} // namespace scr::test::opponentCar

#include "render/render_projection.h"
#include "physics/motion_transforms.h"
#include "track/track_progress.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
int signedWord(std::uint16_t v) noexcept {return v<0x8000?int(v):int(v)-65536;}
std::uint16_t word(int v) noexcept {return static_cast<std::uint16_t>(v);}
int floorShift(int v,unsigned bits) noexcept {const int d=1<<bits;return v/d-(v%d<0);}
struct Polar {std::uint16_t angle{},distance{};};
Polar polar(std::uint16_t x,std::uint16_t z,const RenderProjectionTables& data) {
    const auto ax=word(x&0x8000?-int(x):x),az=word(z&0x8000?-int(z):z);
    if(ax==0x8000||az==0x8000)throw std::out_of_range("Render coordinate exceeds original signed magnitude domain");
    std::uint16_t angle{},ratio{};
    if(ax==az){ratio=0xffff;angle=0x2000;}
    else if(az<ax){
        ratio=std::uint16_t((std::uint32_t(az)<<16)/ax);angle=data.angleTable[ratio>>5];
        if(!((x^z)&0x8000))angle=word(-int(angle));
        angle=word(angle+((x&0x8000)?0xc000:0x4000));
        return {angle,word(int((std::uint32_t(data.magnitudeTable[ratio>>5])*az)>>16)+ax)};
    }else{
        ratio=std::uint16_t((std::uint32_t(ax)<<16)/az);angle=data.angleTable[ratio>>5];
    }
    if((x^z)&0x8000)angle=word(-int(angle));
    if(z&0x8000)angle=word(angle+0x8000);
    return {angle,word(int((std::uint32_t(data.magnitudeTable[ratio>>5])*ax)>>16)+az)};
}
int quantize(double value,double scale) {
    const double result=std::floor(value*scale);
    if(!std::isfinite(result)||result<-1000000||result>1000000)throw std::out_of_range("Render world coordinate outside local geometry domain");
    return static_cast<int>(result);
}
} // namespace
RenderProjectionCamera makeRenderProjectionCamera(const RenderCameraInputs& in,const TrigTable& trig) {
    TrackProgressState local{};local.worldPosition=in.worldPosition;
    updateTrackCamera({in.pitch,in.yaw,in.averageWheelVelocity,0,0},local);
    RenderProjectionCamera camera{};camera.worldPosition=local.worldPosition;
    camera.quarterTurn=local.cameraRotation;camera.heightBaseline=local.cameraHeight;
    camera.yawResidual=local.cameraYaw;camera.viewportCenterY=word(-int(in.pitch));
    camera.rotatedXUnits=local.rotatedX>>12;camera.rotatedZUnits=local.rotatedZ>>12;
    camera.sinRoll=gameSine(in.roll,trig);camera.cosRoll=gameCosine(in.roll,trig);
    camera.sinYaw=gameSine(in.yaw,trig);camera.cosYaw=gameCosine(in.yaw,trig);return camera;
}
RenderProjectedPoint projectRenderCoordinates(std::uint16_t x,std::uint16_t z,std::uint16_t heightDown,
    const RenderProjectionCamera& camera,const RenderProjectionTables& tables) {
    const auto horizontal=polar(x,z,tables);
    const auto relative=word(int(horizontal.angle)-camera.yawResidual);
    const auto side=word(floorShift(signedWord(relative),3));
    const auto down=word(floorShift(signedWord(word(int(polar(heightDown,horizontal.distance,tables).angle)-camera.viewportCenterY)),3));
    const auto rotatedX=word(int(multiplyMatrixWord(side,camera.cosRoll))+multiplyMatrixWord(down,camera.sinRoll));
    const auto rotatedY=word(int(multiplyMatrixWord(down,camera.cosRoll))-multiplyMatrixWord(side,camera.sinRoll));
    const int bearing=signedWord(relative);
    return {floorShift(signedWord(rotatedX),2)+128,floorShift(signedWord(rotatedY),2)+64,horizontal.distance,bearing>-0x4000&&bearing<0x4000};
}
RenderProjectedPoint projectRenderWorldPoint(const RenderWorldPoint& point,const RenderProjectionCamera& camera,const RenderProjectionTables& tables) {
    int x=quantize(point.x,16),z=quantize(point.z,16);
    switch(camera.quarterTurn){
    case 0x4000:{const int old=x;x=32768-z;z=old;break;}
    case 0x8000:x=32768-x;z=32768-z;break;
    case 0xc000:{const int old=x;x=z;z=32768-old;break;}
    default:break;
    }
    const auto relativeX=word(floorShift(x,1)+floorShift(-int(camera.rotatedXUnits),1));
    const auto relativeZ=word(floorShift(z,1)+floorShift(-int(camera.rotatedZUnits),1));
    const auto heightDown=word(floorShift(signedWord(word(int(camera.heightBaseline)-quantize(point.y,32))),3));
    return projectRenderCoordinates(relativeX,relativeZ,heightDown,camera,tables);
}
RenderCameraRelative renderCameraRelative(const RenderWorldPoint& point,const RenderProjectionCamera& camera) noexcept {
    const double dx=point.x-double(camera.worldPosition[0])/65536.0;
    const double dz=point.z-double(camera.worldPosition[2])/65536.0;
    // Camera baseline is measured in raw mesh height words. Divide by64,
    // versus X/Z projection units8 per high-word world unit.
    const double down=double(signedWord(camera.heightBaseline))/64.0-point.y*0.5;
    const double sine=double(signedWord(camera.sinYaw))/32768.0,cosine=double(signedWord(camera.cosYaw))/32768.0;
    return {dx*cosine-dz*sine,down,dx*sine+dz*cosine};
}
double renderCoordinateInverseDepth(std::uint16_t x,std::uint16_t z,std::uint16_t yaw,double units) noexcept{
    constexpr double radians=6.28318530717958647692/65536;
    const auto angle=double(yaw)*radians;
    const auto forward=(signedWord(x)*std::sin(angle)+signedWord(z)*std::cos(angle))/units;
    return 1/std::max(1.0,forward);
}
} // namespace scr

#include "presentation/presentation_interpolation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::runtime {
namespace {
std::uint16_t mixWord(std::uint16_t a,std::uint16_t b,double t){
    const auto wrapped=std::uint16_t(b-a);
    const int delta=wrapped<0x8000?int(wrapped):int(wrapped)-65536;
    return std::uint16_t(std::int64_t(a)+std::llround(delta*t));
}
std::uint32_t mixPosition(std::uint32_t a,std::uint32_t b,double t){
    const auto wrapped=std::uint32_t(b-a);
    const auto delta=wrapped<0x80000000u?std::int64_t(wrapped):std::int64_t(wrapped)-0x100000000LL;
    return std::uint32_t(std::int64_t(a)+std::llround(double(delta)*t));
}
int signedWord(std::uint16_t value){return value<0x8000?int(value):int(value)-65536;}
void mixRoll(const RenderProjectionCamera& a,const RenderProjectionCamera& b,double t,RenderProjectionCamera& out){
    constexpr double pi=3.14159265358979323846;
    const auto angle=[](const RenderProjectionCamera& c){return std::atan2(double(signedWord(c.sinRoll)),double(signedWord(c.cosRoll)));};
    const double start=angle(a),delta=std::remainder(angle(b)-start,2*pi),roll=start+delta*t;
    const auto coefficient=[](double value){return std::uint16_t(int(std::clamp(std::lround(value*32768),-32768L,32767L)));};
    out.sinRoll=coefficient(std::sin(roll));out.cosRoll=coefficient(std::cos(roll));
}
} // namespace
bool canInterpolatePresentation(unsigned a,unsigned b,unsigned fromAction,unsigned toAction) noexcept {
    return a==b&&fromAction==0&&toAction==0;
}
PresentationPose interpolatePresentation(const PresentationPose& from,const PresentationPose& to,double t,const TrigTable& trig){
    if(!std::isfinite(t))throw std::invalid_argument("Replay interpolation fraction must be finite");
    if(t<=0)return from;if(t>=1)return to;
    auto result=from;
    for(unsigned n=0;n<3;++n)result.pose.worldPosition[n]=mixPosition(from.pose.worldPosition[n],to.pose.worldPosition[n],t);
    result.pose.pitch=mixWord(from.pose.pitch,to.pose.pitch,t);result.pose.yaw=mixWord(from.pose.yaw,to.pose.yaw,t);
    result.pose.roll=mixWord(from.pose.roll,to.pose.roll,t);
    result.pose.averageWheelVelocity=mixWord(from.pose.averageWheelVelocity,to.pose.averageWheelVelocity,t);
    result.camera=makeRenderProjectionCamera(result.pose,trig);
    result.camera.viewportCenterY=mixWord(from.camera.viewportCenterY,to.camera.viewportCenterY,t);
    result.camera.heightBaseline=mixWord(from.camera.heightBaseline,to.camera.heightBaseline,t);
    mixRoll(from.camera,to.camera,t,result.camera);
    result.forwardSpeed=mixWord(from.forwardSpeed,to.forwardSpeed,t);
    for(unsigned n=0;n<result.cockpit.objects.size();++n){
        const auto& a=from.cockpit.objects[n];const auto& b=to.cockpit.objects[n];auto& out=result.cockpit.objects[n];
        if(a.widthWords!=b.widthWords)continue;
        out.x=mixWord(a.x,b.x,t);out.y=mixWord(a.y,b.y,t);out.height=mixWord(a.height,b.height,t);
    }
    return result;
}
} // namespace scr::runtime

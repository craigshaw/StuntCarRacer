#include "presentation/camera_view.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::runtime {
PresentationPose makeCameraView(const PresentationPose& source,const CameraViewSettings& settings,const TrigTable& trig){
    auto result=source;
    if(settings.hasCameraOffset()){
        result.pose.yaw=std::uint16_t(int(result.pose.yaw)+settings.yawOffset);
        result.pose.pitch=std::uint16_t(int(result.pose.pitch)+settings.pitchOffset);
        result.pose.roll=std::uint16_t(int(result.pose.roll)+settings.rollOffset);
        result.camera=makeRenderProjectionCamera(result.pose,trig);
    }
    return result;
}
PresentationRectangle fitPresentationImage(int sourceWidth,int sourceHeight,int targetWidth,int targetHeight,bool integerScale){
    if(sourceWidth<=0||sourceHeight<=0)throw std::invalid_argument("Presentation image dimensions must be positive");
    if(targetWidth<=0||targetHeight<=0)return {};
    double scale=std::min(double(targetWidth)/sourceWidth,double(targetHeight)/sourceHeight);
    if(integerScale&&scale>=1)scale=std::floor(scale);
    const int width=std::min(targetWidth,std::max(1,int(std::floor(sourceWidth*scale))));
    const int height=std::min(targetHeight,std::max(1,int(std::floor(sourceHeight*scale))));
    return {(targetWidth-width)/2,(targetHeight-height)/2,width,height};
}
} // namespace scr::runtime

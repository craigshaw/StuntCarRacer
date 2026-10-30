#include "presentation/presentation_interpolation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()try{
    using namespace scr::runtime;
    unsigned cases=0;const auto check=[&](bool ok,const char* message){++cases;if(!ok)throw std::runtime_error(message);};
    scr::TrigTable trig{};PresentationPose a{},b{};
    a.pose.worldPosition={0xffff0000u,100000,200000};b.pose.worldPosition={0x10000u,200000,400000};
    a.pose.yaw=a.pose.pitch=0xfff0;b.pose.yaw=b.pose.pitch=0x10;
    a.camera.sinRoll=std::uint16_t(-286);b.camera.sinRoll=286;a.camera.cosRoll=b.camera.cosRoll=32766;
    a.camera.viewportCenterY=0xffe0;b.camera.viewportCenterY=0x20;
    a.camera.heightBaseline=1200;b.camera.heightBaseline=1400;
    a.cockpit.objects[0]={24,30,2,40};b.cockpit.objects[0]={40,50,2,60};
    a.cockpit.boostActive=0;b.cockpit.boostActive=0x80;b.cockpit.wheelRotation=2;b.cockpit.damageStrip[0]=0xff;
    a.forwardSpeed=1000;b.forwardSpeed=2000;
    const auto m=interpolatePresentation(a,b,0.5,trig);
    check(m.pose.worldPosition==std::array<std::uint32_t,3>{0,150000,300000},"Position interpolation did not follow the wrapped short path");
    check(m.pose.yaw==0&&m.pose.pitch==0,"Angle interpolation crossed a complete turn");
    check(m.camera.sinRoll==0&&m.camera.cosRoll==32767,"Camera roll did not follow the short normalized arc");
    check(m.camera.viewportCenterY==0&&m.camera.heightBaseline==1300,"Original camera baseline/pitch was not interpolated");
    check(m.cockpit.objects[0].x==32&&m.cockpit.objects[0].y==40&&m.cockpit.objects[0].height==50,"Suspension placement did not interpolate");
    check(!m.cockpit.boostActive&&!m.cockpit.wheelRotation&&!m.cockpit.damageStrip[0],"Discrete cockpit events appeared before their verified tick");
    check(m.forwardSpeed==1500,"Speedometer presentation did not interpolate");
    const auto first=interpolatePresentation(a,b,0,trig),last=interpolatePresentation(a,b,1,trig);
    check(first.pose.worldPosition==a.pose.worldPosition&&first.camera.sinRoll==a.camera.sinRoll&&first.cockpit.objects[0].height==40,"First endpoint changed");
    check(last.pose.worldPosition==b.pose.worldPosition&&last.camera.sinRoll==b.camera.sinRoll&&last.cockpit.damageStrip[0]==0xff,"Last endpoint changed");
    check(!canInterpolatePresentation(0,1,0,0)&&!canInterpolatePresentation(1,1,1,0)&&!canInterpolatePresentation(1,1,0,2),"Replay interpolation crossed a restart or terminal event");
    check(canInterpolatePresentation(2,2,0,0),"Ordinary interval unexpectedly held its pose");
    check(interpolatePresentation(a,b,-1,trig).camera.sinRoll==a.camera.sinRoll&&interpolatePresentation(a,b,2,trig).camera.sinRoll==b.camera.sinRoll,"Outside fractions were not clamped");
    bool rejected=false;try{(void)interpolatePresentation(a,b,std::numeric_limits<double>::quiet_NaN(),trig);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Nonfinite fraction was accepted");
    check(a.pose.worldPosition[0]==0xffff0000u&&b.cockpit.damageStrip[0]==0xff,"Interpolation changed its input snapshots");
    std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"wrapped_angles\":true,\"wrapped_positions\":true,\"exact_endpoints\":true,\"discrete_events_held\":true,\"transition_cuts\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

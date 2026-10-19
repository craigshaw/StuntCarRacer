#include "live_drive.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()try{
    using namespace scr::viewer;unsigned cases=0;
    const auto check=[&](bool ok,const char* message){++cases;if(!ok)throw std::runtime_error(message);};
    check(mapDriveControls({},{} )==0,"Neutral input is not neutral");
    check(mapDriveControls({true,false,true,false,true},{})==21,"Keyboard mask differs from original gas/left/boost bits");
    check(mapDriveControls({true,true,true,true,false},{})==2,"Conflicting directions/brake priority were not resolved");
    DrivePad pad;pad.connected=true;pad.x=-7849;pad.y=7849;pad.rightTrigger=30;
    check(mapDriveControls({},pad)==0,"Controller deadzone boundary leaked input");
    pad.x=-7850;pad.y=7850;pad.buttons=0x1000;
    check(mapDriveControls({},pad)==21,"Stick and A-button mapping differs");
    pad={true,7850,-7850,31,0,0x200};
    check(mapDriveControls({},pad)==26,"Brake/right/shoulder mapping differs");
    pad={true,0,0,0,31,0};check(mapDriveControls({},pad)==1,"Right trigger did not accelerate");
    pad={true,0,0,0,0,0x15};check(mapDriveControls({},pad)==5,"D-pad/Start mapping leaked command bits into driving");
    pad.connected=false;check(mapDriveControls({},pad)==0,"Disconnected controller retained input");
    DriveInputLatch latch;latch.sample(16);latch.sample(0);
    check(latch.consume()==16&&latch.consume()==0,"Short boost tap was lost or repeated");
    latch.sample(4);check(latch.consume()==4&&latch.consume()==4,"Held steering did not persist across ticks");
    latch.clear();check(latch.consume()==0,"Focus clear retained a pending input");
    FixedDriveClock a,b;unsigned regular=0,irregular=0;
    for(unsigned n=0;n<60;++n)regular+=a.advance(1.0/60);
    for(double elapsed:{0.07,0.2,0.01,0.35,0.12,0.25})irregular+=b.advance(elapsed);
    check(regular==8&&irregular==8&&std::abs(a.fraction()-b.fraction())<1e-9,"Physics ticks depend on presentation partitioning");
    a.reset();check(a.advance(0.119)==0&&a.advance(0.001)==1,"Physics did not use exact120ms steps");
    check(a.advance(10)==8,"Long stall exceeded bounded ordinary catch-up ticks");
    a.reset();check(a.fraction()==0&&a.advance(0)==0,"Clock reset did not clear old time");
    bool bad=false;try{(void)a.advance(std::numeric_limits<double>::infinity());}catch(const std::invalid_argument&){bad=true;}check(bad,"Nonfinite elapsed time was accepted");
    bad=false;try{(void)a.advance(-1);}catch(const std::invalid_argument&){bad=true;}check(bad,"Negative elapsed time was accepted");
    std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"keyboard_and_gamepad_masks\":true,\"focus_clear\":true,\"tap_latching\":true,\"fixed_tick_partition_independent\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

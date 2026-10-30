#include "input/drive_input.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()try{
    using namespace scr::runtime;unsigned cases=0;
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
    // Both layouts must deliver exactly the same five digital commands to
    // the existing simulation. Exercise every combination, including conflicts.
    for(unsigned mask=0;mask<32;++mask){
        DrivePad original{true, std::int16_t((mask&4)?-20000:(mask&8)?20000:0),
            std::int16_t((mask&1)?20000:(mask&2)?-20000:0),0,0,
            std::uint16_t(((mask&1)?1:0)|((mask&2)?2:0)|((mask&4)?4:0)|((mask&8)?8:0)|((mask&16)?0x1000:0))};
        DrivePad modern=original;modern.y=0;modern.leftTrigger=(mask&2)?255:0;modern.rightTrigger=(mask&1)?255:0;
        modern.buttons&=~3u;
        check(mapDriveControls({},original,DriveControlPreset::Original)==resolveDriveControls(mask),"Original preset changed a digital command");
        check(mapDriveControls({},modern,DriveControlPreset::Modern)==resolveDriveControls(mask),"Modern preset changed a digital command");
        const DriveKeys keyboard{bool(mask&1),bool(mask&2),bool(mask&4),bool(mask&8),bool(mask&16)};
        check(mapDriveControls(keyboard,{},DriveControlPreset::Original)==mapDriveControls(keyboard,{},DriveControlPreset::Modern),"Preset changed keyboard controls");
    }
    pad={true,0,32767,0,0,1};check(mapDriveControls({},pad,DriveControlPreset::Modern)==0,"Modern stick/D-pad up accelerated");
    pad.y=-32768;pad.buttons=2;check(mapDriveControls({},pad,DriveControlPreset::Modern)==0,"Modern stick/D-pad down braked");
    pad={true,0,0,255,255,0};check(mapDriveControls({},pad,DriveControlPreset::Original)==0,"Original preset accepted trigger throttle");
    pad.leftTrigger=0;pad.rightTrigger=30;check(mapDriveControls({},pad,DriveControlPreset::Modern)==0,"Modern trigger threshold leaked input");
    pad.rightTrigger=31;check(mapDriveControls({},pad,DriveControlPreset::Modern)==1,"Modern right trigger threshold failed");
    pad.rightTrigger=255;check(mapDriveControls({},pad,DriveControlPreset::Modern)==1,"Trigger pressure changed digital acceleration");
    pad.leftTrigger=31;check(mapDriveControls({},pad,DriveControlPreset::Modern)==2,"Modern brake lost priority over acceleration");
    pad={true,0,0,30,0,0};check(mapDriveControls({},pad,DriveControlPreset::Modern)==0,"Modern brake threshold leaked input");
    pad.leftTrigger=31;check(mapDriveControls({},pad,DriveControlPreset::Modern)==2,"Modern left trigger threshold failed");
    check(mapDriveControls({true},pad,DriveControlPreset::Modern)==2,"Controller brake lost priority over keyboard gas");
    pad={true,-7849,7849,0,0,0};check(mapDriveControls({},pad,DriveControlPreset::Original)==0,"Original deadzone leaked input");
    check(mapDriveControls({},pad,DriveControlPreset::Modern)==0,"Modern steering deadzone leaked input");
    pad={true,-7850,7850,0,0,0};check(mapDriveControls({},pad,DriveControlPreset::Original)==5,"Original stick threshold failed");
    check(mapDriveControls({},pad,DriveControlPreset::Modern)==4,"Modern diagonal steering changed throttle");
    for(auto preset:{DriveControlPreset::Original,DriveControlPreset::Modern}){
        pad={true,0,0,0,0,0x200};check(mapDriveControls({},pad,preset)==16,"Right shoulder cannot boost");
        pad={false,32767,32767,255,255,0xffff};check(mapDriveControls({},pad,preset)==0,"Disconnected pad affected a preset");
    }
    DriveInputLatch latch;latch.sample(16);latch.sample(0);
    check(latch.consume()==16&&latch.consume()==0,"Short boost tap was lost or repeated");
    latch.sample(4);check(latch.consume()==4&&latch.consume()==4,"Held steering did not persist across ticks");
    latch.clear();check(latch.consume()==0,"Focus clear retained a pending input");
    latch.sample(1);latch.sample(0);
    check(latch.consume(DriveControlPreset::Modern)==0,"Modern replayed gas after release before a physics tick");
    latch.sample(1);latch.sample(0);
    check(latch.consume(DriveControlPreset::Original)==1,"Original lost its short gas press");
    latch.sample(17);latch.sample(0);
    check(latch.consume(DriveControlPreset::Modern)==16,"Modern lost a boost tap or retained released gas");
    latch.sample(1);
    check(latch.consume(DriveControlPreset::Modern)==1&&latch.consume(DriveControlPreset::Modern)==1,"Modern lost held gas during catch-up ticks");
    latch.clear();check(latch.consume(DriveControlPreset::Modern)==0,"Cleared input left Modern accelerating");
    FixedDriveClock a,b;unsigned regular=0,irregular=0;
    for(unsigned n=0;n<60;++n)regular+=a.advance(1.0/60);
    for(double elapsed:{0.07,0.2,0.01,0.35,0.12,0.25})irregular+=b.advance(elapsed);
    check(regular==8&&irregular==8&&std::abs(a.fraction()-b.fraction())<1e-9,"Physics ticks depend on presentation partitioning");
    a.reset();check(a.advance(0.119)==0&&a.advance(0.001)==1,"Physics did not use exact120ms steps");
    check(a.advance(10)==8,"Long stall exceeded bounded ordinary catch-up ticks");
    a.reset();check(a.fraction()==0&&a.advance(0)==0,"Clock reset did not clear old time");
    bool bad=false;try{(void)a.advance(std::numeric_limits<double>::infinity());}catch(const std::invalid_argument&){bad=true;}check(bad,"Nonfinite elapsed time was accepted");
    bad=false;try{(void)a.advance(-1);}catch(const std::invalid_argument&){bad=true;}check(bad,"Negative elapsed time was accepted");
    std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"control_presets\":true,\"keyboard_and_gamepad_masks\":true,\"focus_clear\":true,\"tap_latching\":true,\"fixed_tick_partition_independent\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "presentation/native_cockpit_presentation.h"
#include "race/race_scene.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::runtime;
unsigned cases{};
void check(bool value,const char* message){++cases;if(!value)throw std::runtime_error(message);}
void clock(NativeMemory& m,scr::PackedLapTime time){m[0x1c908]=time.minutes;m[0x1c920]=time.seconds;m[0x1c938]=time.subseconds;}
void expect(NativeCockpitPresentation& hud,const NativeMemory& gameplay,scr::PackedLapTime time){
    const auto original=hud.capture();auto before=std::make_unique<NativeMemory>(gameplay);hud.storeInitialState(*before);
    auto expected=std::make_unique<NativeMemory>(*before);
    drawNativeCockpitLapTime(*expected,time,0x80,2,0);
    check(hud.capture(true)==NativeCockpitPresentation(*expected).capture(),"Hundredths differ from the original font/layout");
    auto after=std::make_unique<NativeMemory>(gameplay);hud.storeInitialState(*after);
    check(*before==*after&&hud.capture()==original,"Enhanced capture mutated retained presentation state");
    const auto enhanced=hud.capture(true);
    for(unsigned n=0;n<enhanced.size();++n)if(enhanced[n]!=original[n]){
        const unsigned x=n%320,y=cockpitInstrumentTop+n/320;
        check(x>=230&&x<288&&y<188,"Running timer changed another instrument or left its panel");
    }
}
void synthetic(const NativeMemory& seed){
    auto game=std::make_unique<NativeMemory>(seed);NativeCockpitPresentation hud(*game);
    using Stage=scr::state::replay::ReplayPresentationStage;
    hud.update(Stage::timing,*game,{});
    check(hud.capture(true)==hud.capture(),"Pre-start clock was exposed before the first line crossing");
    (*game)[0x1bb20]=1;(*game)[0x1bbcc]=(*game)[0x1bbaf]=0;
    for(const auto time:{scr::PackedLapTime{0,0,0},scr::PackedLapTime{0,7,0x13},scr::PackedLapTime{0,7,0x26},
        scr::PackedLapTime{0,9,0x97},scr::PackedLapTime{0,0x10,0x10},scr::PackedLapTime{0,0x59,0x99},scr::PackedLapTime{1,0,0x12}}){
        clock(*game,time);const auto saved=std::make_unique<NativeMemory>(*game);hud.update(Stage::timing,*game,{});expect(hud,*game,time);
        check(*game==*saved,"Running timer changed gameplay memory");
    }
    scr::RaceTimingEffects crossing;crossing.count=1;
    crossing.events[0]={scr::RaceTimingEventKind::lapTimerHud,0x80,{1,0x23,0x45},0x80,2,0};
    (*game)[0x1bb20]=2;(*game)[0x1bbcc]=27;clock(*game,{});
    hud.update(Stage::timing,*game,crossing);expect(hud,*game,{1,0x23,0x45});
    (*game)[0x1bbcc]=26;clock(*game,{0,0,0x13});hud.update(Stage::timing,*game,{});
    expect(hud,*game,{1,0x23,0x45});
    (*game)[0x1bbcc]=0;clock(*game,{0,3,0x51});hud.update(Stage::timing,*game,{});expect(hud,*game,{0,3,0x51});
    // Player finish: retain the corrected event time even after slot clearing
    // and expiration of the ordinary completed-lap hold.
    (*game)[0x1bbaf]=3;(*game)[0x1bbcc]=27;clock(*game,{});hud.update(Stage::timing,*game,crossing);
    (*game)[0x1bbcc]=0;clock(*game,{0,4,0x16});hud.update(Stage::timing,*game,{});expect(hud,*game,{1,0x23,0x45});
    // Opponent finish: freeze the current full precision time at that update.
    (*game)[0x1bbaf]=0;clock(*game,{0,0x10,0x13});hud.update(Stage::timing,*game,{});
    (*game)[0x1bbaf]=3;clock(*game,{0,0x10,0x26});hud.update(Stage::timing,*game,{});
    clock(*game,{0,0x10,0x39});hud.update(Stage::timing,*game,{});expect(hud,*game,{0,0x10,0x26});
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: native_lap_timer_tests assets output");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    auto seed=startNativeRace(argv[1],0);synthetic(*seed.memory);
    RaceScene control,shown;control.loadNativeState(*seed.memory,seed.payload,{});shown.loadNativeState(*seed.memory,seed.payload,{});
    shown.setRunningLapHundredths(true);bool captured=false;unsigned ticks=0;
    for(;ticks<600&&!captured;++ticks){
        check(control.stepLive(1)==shown.stepLive(1),"Timer enhancement changed race lifetime");
        const auto& frame=shown.frames().back();
        shown.renderLive(0.25);shown.renderLive(0.75);shown.render(0);
        if(frame.lapCounter&&frame.minutes==0&&frame.seconds==7&&frame.subseconds){
            const auto enhanced=shown.renderLive(0,false,false);
            check(enhanced!=control.renderLive(0,false,false),"Player hundredths did not reach the rendered HUD");
            shown.setRunningLapHundredths(false);check(shown.render(1)==control.render(1),"Original HUD path differs");
            shown.setRunningLapHundredths(true);check(shown.render(1)==enhanced,"HUD policy toggle reused a stale frame");
            const auto smooth=shown.renderLive(0.5);
            check(smooth!=control.renderLive(0.5),"Smoothed presentation omitted running hundredths");
            shown.setRunningLapHundredths(false);check(shown.renderLive(0.5)==control.renderLive(0.5),"Smoothing changed the original HUD path");
            shown.setRunningLapHundredths(true);check(shown.renderLive(0.5)==smooth,"Smoothed HUD toggle reused a stale frame");
            for(const auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide}){
                shown.renderLive(0,false,false,view);shown.saveBitmap(output/("hundredths-"+std::to_string(unsigned(view))+".bmp"));
                const auto paused=shown.renderLiveMessage(0x4c,2);check(shown.renderLiveMessage(0x4c,2)==paused,"Paused timer changed between presentations");
            }
            captured=true;
        }
    }
    check(captured,"Fresh drive did not reach a running seven-second lap clock");
    for(unsigned n=0;n<600&&!shown.liveTerminal();++n)check(control.stepLive(0,true)==shown.stepLive(0,true),"Timer queries changed race exit");
    check(control.liveTerminal()&&shown.liveTerminal(),"Timer isolation drive did not finish");
    auto expected=std::make_unique<NativeMemory>(),actual=std::make_unique<NativeMemory>();
    control.copyLiveExit(*expected);shown.copyLiveExit(*actual);check(*actual==*expected,"Hundredths changed gameplay, records or RNG");
    std::cout<<"{\"status\":\"passed\",\"cases\":"<<cases<<",\"fresh_ticks\":"<<ticks
        <<",\"original_font_and_layout\":true,\"completed_lap_hold\":true,\"finish_freeze\":true,\"pause_stable\":true,\"simulation_memory_identical\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

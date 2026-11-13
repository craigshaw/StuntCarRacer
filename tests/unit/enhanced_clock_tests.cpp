#include "presentation/enhanced_clock.h"
#include "enhanced_record_fixture.h"
#include <iostream>
#include <limits>

namespace {
using namespace scr;using namespace scr::runtime;namespace fixture=scr::test::enhanced;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
LapTimingBoundary boundary(std::int64_t crossing,std::int64_t observed,std::uint64_t recoveries=0){
    LapTimingBoundary b;b.crossingMicroseconds=crossing;b.observedMicroseconds=observed;b.recoveries=recoveries;b.player=11;b.confidence=timing::Reason::reliable;return b;
}
void timeline(){
    EnhancedClockPresentation clock;auto initial=clock.seed();clock.beginFrame();auto first=boundary(60000,120000);
    clock.observeMovement(boundary(120000,120000));clock.observeTiming(boundary(120000,120000),&first,nullptr,false,false);const auto started=clock.snapshot();
    check(!queryEnhancedClock(initial,&started,59999).lap.available,"Running timer appeared ahead of the car's first crossing");
    check(queryEnhancedClock(initial,&started,60000).lap.available&&queryEnhancedClock(initial,&started,60000).lap.microseconds==0,"First crossing was not displayed at zero");
    check(queryEnhancedClock(started,nullptr,120000).lap.microseconds==60000,"Running display used Original ticks instead of elapsed movement");
    clock.beginFrame();clock.observeMovement(boundary(360000,360000,1));clock.observeTiming(boundary(360000,360000,1),nullptr,nullptr,false,false);const auto recovery=clock.snapshot();
    check(queryEnhancedClock(recovery,nullptr,240000).lap.microseconds==180000,"Future recovery penalty appeared in the earlier car frame");
    check(queryEnhancedClock(recovery,nullptr,359999).lap.microseconds==299999,"Recovery penalty appeared before its presentation boundary");
    check(queryEnhancedClock(recovery,nullptr,360000).lap.microseconds==560000,"Recovery penalty was not applied at the cut boundary");
    clock.beginFrame();clock.observeMovement(boundary(360000,360000,1));const auto afterRecovery=clock.snapshot();
    check(queryEnhancedClock(recovery,&afterRecovery,300000).lap.microseconds==240000,"Next snapshot leaked a future recovery backwards");
    clock.beginFrame();auto finish=boundary(500000,540000,1);LapTimingCompletion completion;completion.enhanced.start=first;completion.enhanced.finish=finish;completion.enhanced.lap=measureEnhancedTime(first,finish);
    clock.observeMovement(boundary(540000,540000,1));clock.observeTiming(boundary(540000,540000,1),&finish,&completion,true,false);const auto completed=clock.snapshot();
    check(queryEnhancedClock(afterRecovery,&completed,499999).lap.microseconds==699999,"Completed result appeared before the presented crossing");
    check(queryEnhancedClock(afterRecovery,&completed,500000).lap.microseconds==700000,"Completed result did not match its scored elapsed time");
    check(queryEnhancedClock(completed,nullptr,540000).record.microseconds==700000,"Completed Enhanced PB was not available in the record display");
    clock.beginFrame();clock.observeMovement(boundary(600000,600000,2));clock.observeTiming(boundary(600000,600000,2),nullptr,nullptr,false,false);const auto released=clock.snapshot();
    check(queryEnhancedClock(completed,&released,599999).lap.microseconds==700000,"Completed-lap hold expired early");
    check(queryEnhancedClock(completed,&released,600000).lap.microseconds==360000,"Next lap lost or double-counted its recovery penalty");
    clock.beginFrame();clock.observeMovement(boundary(720000,720000,2));clock.observeTiming(boundary(720000,720000,2),nullptr,nullptr,false,true);const auto finished=clock.snapshot();
    check(queryEnhancedClock(released,&finished,719999).lap.microseconds==479999,"Opponent finish froze time before its visible update");
    check(queryEnhancedClock(finished,nullptr,900000).lap.microseconds==480000,"Terminal time kept advancing");
    for(auto at:{599999LL,500000LL,540000LL,499999LL,600000LL,500000LL}){
        const auto a=queryEnhancedClock(afterRecovery,&completed,at),b=queryEnhancedClock(afterRecovery,&completed,at);
        check(a.lap.microseconds==b.lap.microseconds&&a.lap.available==b.lap.available,"Repeated/reverse queries mutated clock history");
    }
}
void confidenceAndTotals(){
    EnhancedClockPresentation clock;clock.beginFrame();auto a=boundary(60000,120000);a.confidence=timing::Reason::missingPrevious;
    clock.observeMovement(boundary(120000,120000));clock.observeTiming(boundary(120000,120000),&a,nullptr,false,false);
    check(formatEnhancedClock(queryEnhancedClock(clock.snapshot(),nullptr,120000).lap)=="~0:00.060","Approximate running estimate was not marked");
    clock.beginFrame();auto b=boundary(200000,240000);LapTimingCompletion lap;lap.enhanced.start=a;lap.enhanced.finish=b;lap.enhanced.lap=measureEnhancedTime(a,b);
    lap.enhanced.race=TimedRaceResult{{},fixture::time(59999500)};clock.observeMovement(boundary(240000,240000));clock.observeTiming(boundary(240000,240000),&b,&lap,true,true);
    const auto display=queryEnhancedClock(clock.snapshot(),nullptr,240000);
    check(formatEnhancedClock(display.lap)=="~0:00.140"&&formatEnhancedClock(display.record)=="1:00.000","Approximate completion or once-rounded total differs");
    check(formatEnhancedClock({std::numeric_limits<std::int64_t>::max(),true,false}).find('-')==std::string::npos,"Formatting overflowed at the integer bound");
    check(formatEnhancedClock({})=="--:--.---","Unavailable clock invented a value");
    EnhancedClockPresentation unknown;unknown.records(fixture::time(120000),fixture::time(360000));check(queryEnhancedClock(unknown.seed(),nullptr,0).record.microseconds==120000,"Saved Enhanced record was not restored independently");
}
void composition(){
    CockpitInstrumentPixels original;original.fill(9);std::array<std::uint8_t,96*8> font{};font.fill(0x7e);
    for(const auto value:{EnhancedClockValue{},EnhancedClockValue{12345678,true,false},EnhancedClockValue{12345678,true,true},EnhancedClockValue{7200000000LL,true,false},EnhancedClockValue{std::numeric_limits<std::int64_t>::max(),true,true}}){
        auto pixels=original;composeEnhancedClock(pixels,font,{value,value});bool changed=false;
        for(unsigned n=0;n<pixels.size();++n)if(pixels[n]!=original[n]){changed=true;const auto x=n%320,y=cockpitInstrumentTop+n/320;
            const bool slot=(y>=178&&y<186)||(y>=188&&y<196);const bool corner=y==178||y==185||y==188||y==195;
            check(slot&&x>=(corner?235u:234u)&&x<(corner?283u:284u),"Enhanced clock painted across the slot's border or cut corners");}
        check(changed,"Enhanced clock did not compose its timer fields");
        std::vector<std::uint32_t> finish(64000,0x123456);std::array<std::uint32_t,16> palette{};palette[14]=0xbbbbbb;composeEnhancedFinish(finish,font,palette,{value,value});
        for(unsigned y=168;y<200;++y)for(unsigned x=220;x<296;++x){const auto ink=pixels[(y-cockpitInstrumentTop)*320+x];
            check(finish[y*320+x]==(ink==9?0x123456:palette[ink]),"Saved finish composition changed the slot outline or differed from the live clock");}
    }
    // Six-column digits and narrow punctuation should have one clear pixel
    // between adjacent glyphs, leaving two pixels of paper at either side.
    for(unsigned row=0;row<8;++row){font[(':'-32)*8+row]=row==2||row==5?0x10:0; font[('.'-32)*8+row]=row==5||row==6?0x18:0;}
    auto pixels=original;composeEnhancedClock(pixels,font,{{0,true,false},{0,true,false}});
    std::vector<unsigned> runs;unsigned run=0;
    for(unsigned x=234;x<284;++x){if(pixels[(183-cockpitInstrumentTop)*320+x]==0)++run;else if(run){runs.push_back(run);run=0;}}
    check(runs==std::vector<unsigned>({6,1,6,6,2,6,6,6}),"Normal times squeezed native digits or left oversized punctuation cells");
    for(unsigned x:{234u,235u,242u,244u,251u,258u,261u,268u,275u,282u,283u})
        check(pixels[(183-cockpitInstrumentTop)*320+x]==14,"Normal timer glyph spacing or slot padding differs");
}
}
int main()try{timeline();confidenceAndTotals();composition();std::cout<<"Enhanced clock: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

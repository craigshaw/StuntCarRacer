#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "windows_game_display.h"
#include "native_game_session.h"
#include "replay_view.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using namespace scr::viewer;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Surface {
    HDC dc{CreateCompatibleDC(nullptr)};HBITMAP bitmap{};HGDIOBJ previous{};std::uint32_t* pixels{};int width,height;
    Surface(int w,int h):width(w),height(h){
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        require(dc&&bitmap,"Cannot create display test surface");previous=SelectObject(dc,bitmap);
    }
    ~Surface(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}
};
void tap(NativeDisplaySettings& settings,unsigned controls){settings.navigate(0);settings.navigate(controls);settings.navigate(0);}
void storage(const std::filesystem::path& output){
    const auto file=output/"preferences.ini";
    for(bool fullscreen:{false,true})for(bool smooth:{false,true})for(unsigned scale=0;scale<3;++scale)
        for(auto preset:{DriveControlPreset::Original,DriveControlPreset::Modern})for(auto navigation:{NativeMenuNavigation::Original,NativeMenuNavigation::Responsive}){
        NativeDisplayPreferences preferences{fullscreen,smooth,NativeScaling(scale),preset,navigation};
        if(!saveDisplayPreferences(file,preferences))throw std::runtime_error("Display preference test write failed: Windows error "+std::to_string(GetLastError()));
        require(loadDisplayPreferences(file)==preferences,"Display preference roundtrip differs");
    }
    {std::ofstream out(file);out<<"SCR display 1\nfullscreen=1\nscaling=smooth\ninterpolate=1\n";}
    require(loadDisplayPreferences(file)==NativeDisplayPreferences{true,true,NativeScaling::FitSmooth,DriveControlPreset::Original},"Legacy preferences lost display choices or enabled Modern");
    {std::ofstream out(file);out<<"SCR display 2\nfullscreen=1\nscaling=sharp\ninterpolate=0\ncontrols=modern\n";}
    require(loadDisplayPreferences(file)==NativeDisplayPreferences{true,false,NativeScaling::FitSharp,DriveControlPreset::Modern,NativeMenuNavigation::Responsive},"Version 2 migration lost choices or did not adopt responsive navigation");
    for(const std::string content:std::vector<std::string>{"", "SCR display 2\nfullscreen=0\nscaling=integer\ninterpolate=0\n",
        "SCR display 1\nfullscreen=2\nscaling=integer\ninterpolate=0\n", "SCR display 1\nfullscreen=0\nscaling=unknown\ninterpolate=0\n",
        "SCR display 1\nfullscreen=0\nscaling=integer\n", "SCR display 1\nfullscreen=0\nscaling=integer\ninterpolate=0\nfullscreen=1\n",
        "SCR display 2\nfullscreen=0\nscaling=integer\ninterpolate=0\ncontrols=unknown\n",
        "SCR display 3\nfullscreen=0\nscaling=integer\ninterpolate=0\ncontrols=modern\nnavigation=unknown\n",
        "SCR display 3\nfullscreen=0\nscaling=integer\ninterpolate=0\ncontrols=modern\n",
        "SCR display 2\nfullscreen=0\nscaling=integer\ninterpolate=0\ncontrols=modern\ncontrols=original\n",std::string(2048,'x')}){
        {std::ofstream out(file);out<<content;}
        require(loadDisplayPreferences(file)==NativeDisplayPreferences{},"Invalid preferences were applied");
    }
    const NativeDisplayPreferences previous{true,true,NativeScaling::FitSmooth,DriveControlPreset::Modern};
    if(!saveDisplayPreferences(file,previous))throw std::runtime_error("Cannot seed atomic preferences: Windows error "+std::to_string(GetLastError()));
    const auto pending=output/"preferences.ini.tmp";std::filesystem::create_directory(pending);
    require(!saveDisplayPreferences(file,{})&&loadDisplayPreferences(file)==previous,"Failed preference write replaced the last good file");
    std::filesystem::remove(pending);
    bool rejected=false;try{saveDisplayPreferences(output/"original.adf",{});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Display storage accepted an original-media filename");
}
void navigation(){
    NativeDisplaySettings settings;DrivePad pad{true};pad.buttons=0x1000;
    settings.openSettings(displayPadNavigation(pad));require(!settings.navigate(displayPadNavigation(pad))&&!settings.preferences().fullscreen,"Held controller A activated newly opened settings");
    settings.navigate(0);tap(settings,displayPadNavigation(pad));require(settings.preferences().fullscreen,"Controller A cannot change a setting");
    pad.buttons=0x0002;tap(settings,displayPadNavigation(pad));require(settings.selected()==1,"Controller down did not select scaling");
    pad.buttons=0x0008;tap(settings,displayPadNavigation(pad));require(settings.preferences().scaling==NativeScaling::FitSharp,"Controller right did not change scaling");
    settings.navigate(8);require(settings.preferences().scaling==NativeScaling::FitSmooth,"New right press was ignored");
    settings.navigate(8);require(settings.preferences().scaling==NativeScaling::FitSmooth,"Held direction repeated settings changes");settings.navigate(0);
    tap(settings,2);tap(settings,8);require(settings.preferences().interpolate,"Motion setting did not preview");
    tap(settings,2);tap(settings,8);require(settings.preferences().controls==DriveControlPreset::Modern,"Controls setting did not select Modern");
    tap(settings,4);require(settings.preferences().controls==DriveControlPreset::Original,"Controls setting did not restore Original");
    tap(settings,16);require(settings.preferences().controls==DriveControlPreset::Modern,"Confirm cannot toggle control presets");
    tap(settings,2);tap(settings,8);require(settings.preferences().navigation==NativeMenuNavigation::Original,"Menu navigation setting did not select Original");
    tap(settings,8);require(settings.preferences().navigation==NativeMenuNavigation::Responsive,"Menu navigation setting did not restore Responsive");
    tap(settings,2);settings.navigate(0);require(settings.navigate(16)&&settings.blocksGame(),"Apply did not request saving while blocking gameplay");
    settings.applied();require(!settings.blocksGame(),"Applied direct settings did not return to game");
    const auto saved=settings.preferences();settings.pause();tap(settings,2);tap(settings,16);
    require(settings.page()==NativeDisplayPage::Settings,"Pause menu did not open settings");tap(settings,8);settings.back();
    require(settings.page()==NativeDisplayPage::Pause&&settings.preferences()==saved,"Cancel did not restore preferences and paused state");settings.back();
    settings.openSettings();tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,8);
    require(settings.preferences().controls==DriveControlPreset::Original,"Controls did not preview on reopening settings");
    tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,16);
    require(!settings.blocksGame()&&settings.preferences()==saved,"Cancel row did not restore the saved control preset");
    settings.openSettings();tap(settings,8);settings.suspendRace();settings.applied();
    require(settings.page()==NativeDisplayPage::Pause,"Focus loss allowed settings to resume a race");
    pad.buttons=0x2000;tap(settings,displayPadNavigation(pad));require(!settings.blocksGame(),"Controller B cannot resume pause");
    pad.buttons=0;pad.x=-20000;pad.y=20000;require(displayPadNavigation(pad)==5,"Controller stick navigation differs");
    pad.connected=false;require(!displayPadNavigation(pad),"Disconnected controller produced navigation");
    settings.pause();settings.releaseNavigation();settings.navigate(16);
    require(settings.page()==NativeDisplayPage::Pause,"Held fire resumed after focus loss");
    settings.navigate(0);settings.navigate(16);require(!settings.blocksGame(),"Fresh fire could not resume after focus loss");
}
double filterMilliseconds{};
unsigned scaling(WindowsGameDisplay& display){
    std::vector<std::uint32_t> source(64000);for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)source[y*320+x]=((x/8+y/8)&1)?0xff2020:0x2040ff;
    const auto original=source;unsigned cases=0;
    for(const auto size:std::vector<std::pair<int,int>>{{800,500},{1920,1080},{319,199},{1280,1024}})for(auto mode:{NativeScaling::Integer,NativeScaling::FitSharp,NativeScaling::FitSmooth}){
        Surface surface(size.first,size.second);display.paint(surface.dc,size.first,size.second,source,mode);GdiFlush();
        const auto rectangle=fitPresentationImage(320,200,size.first,size.second,mode==NativeScaling::Integer);
        unsigned blended=0;
        for(int y=0;y<size.second;++y)for(int x=0;x<size.first;++x){
            const auto colour=surface.pixels[y*size.first+x]&0xffffff;
            const bool inside=x>=rectangle.x&&x<rectangle.x+rectangle.width&&y>=rectangle.y&&y<rectangle.y+rectangle.height;
            if(!inside)require(!colour,"Scaling painted outside letterboxing");
            else if(mode==NativeScaling::FitSmooth)blended+=colour!=0xff2020&&colour!=0x2040ff;
            else require(colour==0xff2020||colour==0x2040ff,"Sharp scaling introduced filtered colours");
            if(mode==NativeScaling::Integer&&size.first==800&&inside){const int sx=(x-rectangle.x)/2,sy=(y-rectangle.y)/2;require(colour==source[sy*320+sx],"Integer scaling changed source pixel replication");}
        }
        if(mode==NativeScaling::FitSmooth)require(blended>0,"Smooth scaling did not filter pixel edges");
        require(source==original,"Display scaling modified game pixels");++cases;
    }
    Surface surface(800,500);display.paint(surface.dc,800,500,source,NativeScaling::Integer);GdiFlush();
    const std::vector<std::uint32_t> baseline(surface.pixels,surface.pixels+800*500);
    NativeDisplaySettings settings;settings.openSettings();display.overlay(surface.dc,800,500,settings,L"");GdiFlush();
    require(!std::equal(baseline.begin(),baseline.end(),surface.pixels)&&source==original,"Overlay did not compose separately from game pixels");
    Surface hd(1920,1080);const auto began=std::chrono::steady_clock::now();
    for(unsigned n=0;n<32;++n){source[320*100+160]^=1;display.paint(hd.dc,1920,1080,source,NativeScaling::FitSmooth);GdiFlush();}
    filterMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/32;
    return cases;
}
void fullscreen(WindowsGameDisplay& display){
    // This hidden test window exercises window state without desktop input.
    const auto window=CreateWindowExW(0,L"STATIC",L"Display placement test",WS_OVERLAPPEDWINDOW,120,100,800,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"Cannot create placement test window");RECT before{};GetWindowRect(window,&before);
    for(unsigned n=0;n<4;++n){
        display.fullscreen(window,true);RECT actual{};GetWindowRect(window,&actual);MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        require(EqualRect(&actual,&monitor.rcMonitor)&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_OVERLAPPEDWINDOW),"Borderless fullscreen did not cover its monitor");
        display.fullscreen(window,false);GetWindowRect(window,&actual);require(EqualRect(&actual,&before),"Leaving fullscreen did not restore the window rectangle");
    }
    DestroyWindow(window);
}
void settle(NativeGameSession& game){for(unsigned n=0;game.transitioning()&&n<16;++n)game.update(1,{});require(!game.transitioning(),"Race setup fade did not finish");}
void fire(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});game.update(0,{{16,0},0,false});game.update(1,{});settle(game);}
void start(NativeGameSession& game){settle(game);fire(game);fire(game);fire(game);game.update(1,{});settle(game);require(game.page()==NativeMenuPage::Preview,"Display test did not reach preview");game.update(0,{});game.update(0,{{16,0},0,false});settle(game);require(game.page()==NativeMenuPage::Race,"Display test did not enter Practice");}
void timing(const std::filesystem::path& pack,const std::filesystem::path& output,WindowsGameDisplay& display){
    NativeGameSession viewed(pack,output/"viewed",NativeGameStart::EstablishedMenu),twin(pack,output/"twin",NativeGameStart::EstablishedMenu);start(viewed);start(twin);
    NativeDisplaySettings settings;Surface surface(800,500);
    for(unsigned tick=0;tick<96;++tick){
        if(tick==32){
            viewed.update(0.06,{});twin.update(0.06,{});const auto before=viewed.race().liveTicks();const auto image=viewed.render(true);
            settings.openSettings();
            for(unsigned n=0;n<120;++n){
                if(!settings.blocksGame())viewed.update(1.0/60,{});
                display.paint(surface.dc,800,500,viewed.render(true,true),NativeScaling(n%3));display.overlay(surface.dc,800,500,settings,L"");
            }
            require(viewed.race().liveTicks()==before,"Settings advanced physics ticks");settings.back();
            require(viewed.render(true)==image,"Settings contaminated the underlying race image");
            viewed.update(0.059,{});twin.update(0.059,{});require(viewed.race().liveTicks()==before,"Settings changed the retained sub-tick phase");
            viewed.update(0.001,{});twin.update(0.001,{});require(viewed.race().liveTicks()==before+1,"Resuming lost the original tick boundary");
        }
        viewed.update(0.12,{{},17,false});twin.update(0.12,{{},17,false});
        require(viewed.race().liveTicks()==twin.race().liveTicks(),"Display settings changed later physics scheduling");
    }
    for(unsigned n=0;n<600&&viewed.page()==NativeMenuPage::Race&&!viewed.transitioning()&&!viewed.raceEnded();++n){viewed.update(0.12,{{},0,true});twin.update(0.12,{{},0,true});}
    if(viewed.raceEnded()){fire(viewed);fire(twin);}settle(viewed);settle(twin);
    require(viewed.page()==NativeMenuPage::Track&&twin.page()==NativeMenuPage::Track,"Display test did not return through the original quit gate");
    require(viewed.memory()==twin.memory(),"Display settings altered final game RAM");
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_display_tests asset-pack output-directory");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    storage(output);navigation();WindowsGameDisplay display;const auto cases=scaling(display);fullscreen(display);timing(argv[1],output,display);
    std::cout<<"{\"status\":\"passed\",\"preference_roundtrips\":48,\"corrupt_preferences\":11,\"legacy_preferences_migrated\":true,\"control_preset_navigation\":true,\"atomic_failure_checked\":true,\"controller_navigation\":true,\"scaling_cases\":"<<cases
        <<",\"fullscreen_roundtrips\":4,\"paused_presentations\":120,\"changing_1080p_filter_ms\":"<<filterMilliseconds<<",\"original_tick_phase_preserved\":true,\"final_simulation_memory_identical\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

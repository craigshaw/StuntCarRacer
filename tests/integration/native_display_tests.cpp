#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "platform/windows/game_display.h"
#include "session/native_game_session.h"
#include "race/race_transitions.h"
#include "state/race_transitions_adapter.h"
#include "presentation/camera_view.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using namespace scr::runtime;
double ultrawideFilterMilliseconds{};
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
    void save(const std::filesystem::path& path)const{
        std::ofstream out(path,std::ios::binary);
        const auto u16=[&](unsigned n){out.put(char(n));out.put(char(n>>8));};
        const auto u32=[&](unsigned n){u16(n);u16(n>>16);};
        out.write("BM",2);u32(54+width*height*4);u32(0);u32(54);u32(40);u32(width);u32(unsigned(-height));u16(1);u16(32);
        u32(0);u32(width*height*4);u32(2835);u32(2835);u32(0);u32(0);
        out.write(reinterpret_cast<const char*>(pixels),std::streamsize(width*height*4));require(bool(out),"Cannot save native display capture");
    }
};
void widescreenDisplay(WindowsGameDisplay& display,const std::filesystem::path& pack,const std::filesystem::path& output,NativeView view){
    const bool ultra=view==NativeView::Ultrawide;const int host=ultra?3840:1920,target=ultra?2560:1280;
    const unsigned width=unsigned(viewWidth(view)),offset=(width-320)/2;
    NativeDisplayPreferences prefs;prefs.view=view;prefs.scaling=NativeScaling::Fit;
    const auto fit=displayRectangle(host,1080,prefs);require(fit.width==host&&fit.height==1080,"Wide Fit has the wrong aspect ratio");
    const auto menu=displayRectangle(host,1080,prefs,320);require(menu.width==1440&&menu.x==(host-1440)/2,"Wide view stretched an original menu");
    prefs.scaling=NativeScaling::Integer;const auto automatic=displayRectangle(host,1080,prefs);
    require(automatic.width==displayWidthAtScale(view,4)&&automatic.height==960,"Wide Integer Auto did not preserve the source scale");
    require(displayWidthAtScale(view,1)==(ultra?853:427)&&displayWidthAtScale(view,2)==(ultra?1707:853)&&displayWidthAtScale(view,3)==target,"Wide fixed window widths differ");
    std::vector<std::uint32_t> source(64000),wide(width*200,0x155131);
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)
        wide[y*width+offset+x]=source[y*320+x]=((x/7+y/5)&1)?0xff2020:0x2040ff;
    for(auto mode:{NativeScaling::Integer,NativeScaling::Fit,NativeScaling::Stretch})
    for(auto filtering:{NativeFiltering::NearestNeighbour,NativeFiltering::Bilinear}){
        prefs.scaling=mode;prefs.filtering=filtering;Surface actual(target,720),reference(960,720);
        auto original=prefs;original.view=NativeView::Original;
        display.paint(actual.dc,target,720,wide,prefs);display.paint(reference.dc,960,720,source,original);GdiFlush();
        // Bilinear edge samples may blend against the added side pixels. The
        // rest of the original canvas must retain precisely the same scale.
        const int edge=mode!=NativeScaling::Integer&&filtering==NativeFiltering::Bilinear?4:0;
        for(int y=0;y<720;++y)for(int x=edge;x<960-edge;++x)
            require((actual.pixels[y*target+x+(target-960)/2]&0xffffff)==(reference.pixels[y*960+x]&0xffffff),"Wide view stretched or rescaled the original car canvas");
    }
    prefs.scaling=NativeScaling::Fit;prefs.filtering=NativeFiltering::Bilinear;
    Surface larger(target+640,720);display.paint(larger.dc,target+640,720,wide,prefs);GdiFlush();
    for(int y=0;y<720;++y)for(int x=0;x<target+640;++x)if(x<320||x>=target+320)
        require(!(larger.pixels[y*(target+640)+x]&0xffffff),"Wide canvas overdraw escaped its aspect aperture");
    RaceScene scene;scene.loadNative(pack,{},6);
    scene.setRunningLapHundredths(true);
    for(unsigned tick=0;tick<=128;++tick)require(scene.stepLive(1|(tick%40<8?16:0)|(tick>100&&tick%40<4?4:0)),"Display capture race ended early");
    prefs.filtering=NativeFiltering::NearestNeighbour;Surface gameplay(target,720);
    display.paint(gameplay.dc,target,720,scene.renderLive(0,false,false,view),prefs);GdiFlush();
    gameplay.save(output/(ultra?"32x9-gameplay.bmp":"16x9-gameplay.bmp"));
    if(ultra){
        prefs.filtering=NativeFiltering::Bilinear;Surface g9(5120,1440);
        const auto began=std::chrono::steady_clock::now();
        for(unsigned n=0;n<8;++n){wide[100*width+width/2]^=1;display.paint(g9.dc,5120,1440,wide,prefs);GdiFlush();}
        ultrawideFilterMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/8;
    }
}
void tap(NativeDisplaySettings& settings,unsigned controls){settings.navigate(0);settings.navigate(controls);settings.navigate(0);}
void storage(const std::filesystem::path& output){
    const auto file=output/"preferences.ini";
    for(bool fullscreen:{false,true})for(bool smooth:{false,true})for(unsigned scale=0;scale<3;++scale)
        for(auto preset:{DriveControlPreset::Original,DriveControlPreset::Modern})for(auto navigation:{NativeMenuNavigation::Original,NativeMenuNavigation::Responsive})
        for(auto filtering:{NativeFiltering::NearestNeighbour,NativeFiltering::Bilinear})for(unsigned integer:{0u,1u,3u,maximumIntegerScale}){
        NativeDisplayPreferences preferences{fullscreen,smooth,NativeScaling(scale),preset,navigation,filtering,integer};
        if(!saveDisplayPreferences(file,preferences))throw std::runtime_error("Display preference test write failed: Windows error "+std::to_string(GetLastError()));
        require(loadDisplayPreferences(file)==preferences,"Display preference roundtrip differs");
        // Exercise every choice without hammering one path faster than Windows
        // filesystem filters release it. A failed write still fails the test.
        Sleep(10);
    }
    NativeDisplayPreferences wide;wide.view=NativeView::Extended;
    require(saveDisplayPreferences(file,wide)&&loadDisplayPreferences(file)==wide,"Extended preference did not roundtrip");
    wide.view=NativeView::Widescreen;
    require(saveDisplayPreferences(file,wide)&&loadDisplayPreferences(file)==wide,"Widescreen preference did not roundtrip");
    wide.view=NativeView::Ultrawide;
    require(saveDisplayPreferences(file,wide)&&loadDisplayPreferences(file)==wide,"Ultrawide preference did not roundtrip");
    const std::string modernFields="fullscreen=1\nscaling=fit\ninterpolate=1\ncontrols=modern\nnavigation=responsive\nfiltering=nearest\ninteger-scale=2\n";
    {std::ofstream out(file);out<<"SCR display 4\n"<<modernFields;}
    require(loadDisplayPreferences(file).view==NativeView::Original&&loadDisplayPreferences(file).integerScale==2,"Version 4 aspect migration lost the original view or scale");
    {std::ofstream out(file);out<<"SCR display 5\n"<<modernFields<<"view=widescreen\n";}
    require(loadDisplayPreferences(file).view==NativeView::Extended&&loadDisplayPreferences(file).fullscreen,"Old prototype view did not migrate to Extended");
    {std::ofstream out(file);out<<"SCR display 6\n"<<modernFields<<"aspect=extended\n";}
    require(loadDisplayPreferences(file).view==NativeView::Extended,"Version 6 Extended selection was lost");
    {std::ofstream out(file);out<<"SCR display 7\n"<<modernFields<<"aspect=widescreen\n";}
    require(loadDisplayPreferences(file).view==NativeView::Widescreen,"Version 7 widescreen selection was lost");
    for(const auto header:{"SCR display 7\n","SCR display 8\n"}){
        {std::ofstream out(file);out<<header<<modernFields<<"aspect=unknown\n";}
        require(loadDisplayPreferences(file)==NativeDisplayPreferences{},"Unknown aspect was accepted");
    }
    for(const auto suffix:{"aspect=unknown\n","","aspect=extended\nextra=1\n"}){
        {std::ofstream out(file);out<<"SCR display 6\n"<<modernFields<<suffix;}
        require(loadDisplayPreferences(file)==NativeDisplayPreferences{},"Malformed aspect preferences were applied");
    }
    {std::ofstream out(file);out<<"SCR display 1\nfullscreen=1\nscaling=smooth\ninterpolate=1\n";}
    require(loadDisplayPreferences(file)==NativeDisplayPreferences{true,true,NativeScaling::Fit,DriveControlPreset::Original,NativeMenuNavigation::Responsive,NativeFiltering::Bilinear},"Legacy preferences lost display choices or enabled Modern");
    {std::ofstream out(file);out<<"SCR display 2\nfullscreen=1\nscaling=sharp\ninterpolate=0\ncontrols=modern\n";}
    require(loadDisplayPreferences(file)==NativeDisplayPreferences{true,false,NativeScaling::Fit,DriveControlPreset::Modern,NativeMenuNavigation::Responsive},"Version 2 migration lost choices or did not adopt responsive navigation");
    {std::ofstream out(file);out<<"SCR display 3\nfullscreen=0\nscaling=integer\ninterpolate=1\ncontrols=modern\nnavigation=original\n";}
    require(loadDisplayPreferences(file)==NativeDisplayPreferences{false,true,NativeScaling::Integer,DriveControlPreset::Modern,NativeMenuNavigation::Original},"Version 3 did not retain Integer Auto and navigation");
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
    for(const std::string suffix:{"filtering=unknown\ninteger-scale=0\n","filtering=nearest\ninteger-scale=33\n","filtering=nearest\ninteger-scale=-1\n",
        "filtering=nearest\ninteger-scale=3junk\n","filtering=nearest\ninteger-scale=\n","filtering=nearest\n"}){
        {std::ofstream out(file);out<<"SCR display 4\nfullscreen=1\nscaling=stretch\ninterpolate=1\ncontrols=modern\nnavigation=original\n"<<suffix;}
        require(loadDisplayPreferences(file)==NativeDisplayPreferences{},"Invalid filtering or integer scale was applied");
    }
    const NativeDisplayPreferences previous{true,true,NativeScaling::Stretch,DriveControlPreset::Modern,NativeMenuNavigation::Original,NativeFiltering::Bilinear,3};
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
    pad.buttons=0x0008;tap(settings,displayPadNavigation(pad));require(settings.preferences().scaling==NativeScaling::Fit,"Controller right did not change scaling");
    settings.navigate(8);require(settings.preferences().scaling==NativeScaling::Stretch,"New right press was ignored");
    settings.navigate(8);require(settings.preferences().scaling==NativeScaling::Stretch,"Held direction repeated settings changes");settings.navigate(0);
    tap(settings,2);tap(settings,8);require(settings.preferences().filtering==NativeFiltering::Bilinear,"Filtering did not preview independently");
    tap(settings,1);tap(settings,8);tap(settings,2);tap(settings,8);
    require(settings.preferences().scaling==NativeScaling::Integer&&settings.preferences().integerScale==1&&settings.preferences().filtering==NativeFiltering::Bilinear,"Integer scale or retained filter differs");
    tap(settings,4);require(settings.preferences().integerScale==0,"Left did not restore Integer Auto");
    tap(settings,2);tap(settings,8);require(settings.preferences().interpolate,"Motion setting did not preview");
    tap(settings,2);tap(settings,8);require(settings.preferences().controls==DriveControlPreset::Modern,"Controls setting did not select Modern");
    tap(settings,4);require(settings.preferences().controls==DriveControlPreset::Original,"Controls setting did not restore Original");
    tap(settings,16);require(settings.preferences().controls==DriveControlPreset::Modern,"Confirm cannot toggle control presets");
    tap(settings,2);tap(settings,8);require(settings.preferences().navigation==NativeMenuNavigation::Original,"Menu navigation setting did not select Original");
    tap(settings,8);require(settings.preferences().navigation==NativeMenuNavigation::Responsive,"Menu navigation setting did not restore Responsive");
    tap(settings,2);tap(settings,8);require(settings.preferences().view==NativeView::Extended,"Aspect ratio did not select Extended");
    tap(settings,4);require(settings.preferences().view==NativeView::Original,"Aspect ratio did not restore Original");
    tap(settings,4);require(settings.preferences().view==NativeView::Ultrawide,"Aspect ratio cannot select 32:9 backwards");
    tap(settings,4);require(settings.preferences().view==NativeView::Widescreen,"Aspect ratio cannot select 16:9 backwards");
    tap(settings,8);require(settings.preferences().view==NativeView::Ultrawide,"Aspect ratio did not select 32:9");
    tap(settings,8);require(settings.preferences().view==NativeView::Original,"Aspect ratio did not cycle from 32:9 to Original");
    tap(settings,2);settings.navigate(0);require(settings.navigate(16)&&settings.blocksGame(),"Apply did not request saving while blocking gameplay");
    settings.applied();require(!settings.blocksGame(),"Applied direct settings did not return to game");
    const auto saved=settings.preferences();settings.pause();tap(settings,2);tap(settings,16);
    require(settings.page()==NativeDisplayPage::Settings,"Pause menu did not open settings");tap(settings,8);settings.back();
    require(settings.page()==NativeDisplayPage::Pause&&settings.preferences()==saved,"Cancel did not restore preferences and paused state");settings.back();
    settings.openSettings();tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,8);
    require(settings.preferences().controls==DriveControlPreset::Original,"Controls did not preview on reopening settings");
    tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,16);
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
void gameMenu(){
    require(gameMenuShortcut(VK_ESCAPE,false,true,false)&&gameMenuShortcut(VK_ESCAPE,false,false,true),"Esc did not open the Game menu in a race or at the mode chooser");
    require(!gameMenuShortcut(VK_ESCAPE,false,false,false)&&!gameMenuShortcut(VK_ESCAPE,true,false,false),"Esc intercepted Back in a nested menu or name editor");
    require(gameMenuShortcut('P',false,false,false)&&gameMenuShortcut('P',false,true,false)&&!gameMenuShortcut('P',true,false,false),"P shortcut interfered with name entry or stopped opening the Game menu");
    NativeDisplaySettings menu;menu.openGameMenu(false,16,NativeQuitContext::Multiplayer);
    menu.navigate(16);require(menu.optionCount()==3&&menu.selected()==0&&menu.takeAction()==NativeDisplayAction::None,"Held confirm activated the newly opened non-race menu");
    menu.navigate(0);tap(menu,1);require(menu.selected()==2,"Non-race menu did not expose Quit as its final action");
    menu.navigate(16);menu.navigate(16);
    require(menu.page()==NativeDisplayPage::QuitConfirmation&&menu.selected()==0&&menu.quitContext()==NativeQuitContext::Multiplayer,"Quit confirmation did not start on Cancel or retained confirm activated it");
    menu.navigate(0);tap(menu,16);require(menu.page()==NativeDisplayPage::Pause&&menu.selected()==2&&menu.takeAction()==NativeDisplayAction::None,"Cancel did not restore the Quit selection");
    tap(menu,16);tap(menu,32);require(menu.page()==NativeDisplayPage::Pause&&menu.selected()==2,"Escape did not cancel Quit");
    tap(menu,16);menu.suspendRace(0,false,NativeQuitContext::Career);menu.releaseNavigation();menu.navigate(16);
    require(menu.page()==NativeDisplayPage::QuitConfirmation&&menu.selected()==0,"Focus loss dismissed or confirmed Quit");
    menu.navigate(0);tap(menu,2);tap(menu,16);
    require(!menu.blocksGame()&&menu.takeAction()==NativeDisplayAction::Quit&&menu.takeAction()==NativeDisplayAction::None,"Confirmed Quit was missing or dispatched more than once");
    menu.openGameMenu(true,0,NativeQuitContext::CareerRace);require(menu.optionCount()==4,"Race menu did not expose Leave Race");
    tap(menu,2);tap(menu,16);require(menu.page()==NativeDisplayPage::Settings,"Game menu did not open Settings");
    const auto original=menu.preferences();tap(menu,8);menu.back();
    require(menu.page()==NativeDisplayPage::Pause&&menu.selected()==1&&menu.preferences()==original&&menu.canLeaveRace(),"Settings Cancel did not return to the same Game menu");
    tap(menu,2);tap(menu,16);require(!menu.blocksGame()&&menu.takeAction()==NativeDisplayAction::LeaveRace,"Leave Race did not dispatch the original race-exit request");
    menu.openGameMenu(false,32);menu.navigate(32);require(menu.blocksGame(),"Held Esc immediately closed the Game menu");
    menu.navigate(0);tap(menu,32);require(!menu.blocksGame(),"Fresh Esc could not resume from the Game menu");
}
double filterMilliseconds{};
unsigned scaling(WindowsGameDisplay& display){
    std::vector<std::uint32_t> source(64000);for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)source[y*320+x]=((x/8+y/8)&1)?0xff2020:0x2040ff;
    const auto original=source;unsigned cases=0;
    NativeDisplayPreferences extended;extended.view=NativeView::Extended;extended.scaling=NativeScaling::Fit;
    const auto extendedFit=displayRectangle(1920,1080,extended);
    require(extendedFit.width==1440&&extendedFit.height==1080,"Extended Fit did not preserve 4:3");
    extended.view=NativeView::Original;const auto originalFit=displayRectangle(1920,1080,extended);
    require(originalFit.width==extendedFit.width&&originalFit.height==extendedFit.height,"Changing aspect mode changed the cockpit scale");
    NativeDisplayPreferences prefs;
    auto rectangle=displayRectangle(1920,1080,prefs);require(rectangle.width==1280&&rectangle.height==960&&rectangle.x==320&&rectangle.y==60,"1080p Integer Auto is not centred aspect-corrected 4x");
    prefs.integerScale=3;rectangle=displayRectangle(1920,1080,prefs);require(rectangle.width==960&&rectangle.height==720&&rectangle.x==480&&rectangle.y==180,"Fixed 3x geometry differs");
    prefs.integerScale=32;rectangle=displayRectangle(1920,1080,prefs);require(rectangle.width==1280&&rectangle.height==960,"Oversized fixed scale was not limited to fit");
    prefs.scaling=NativeScaling::Fit;rectangle=displayRectangle(1920,1080,prefs);require(rectangle.width==1440&&rectangle.height==1080&&rectangle.x==240,"Fit did not preserve 4:3");
    prefs.scaling=NativeScaling::Stretch;rectangle=displayRectangle(1920,1080,prefs);require(rectangle.width==1920&&rectangle.height==1080&&rectangle.x==0&&rectangle.y==0,"Stretch did not fill both axes");
    for(const auto size:std::vector<std::pair<int,int>>{{800,500},{1920,1080},{319,199},{1280,1024}})for(auto mode:{NativeScaling::Integer,NativeScaling::Fit,NativeScaling::Stretch})
        for(auto filtering:{NativeFiltering::NearestNeighbour,NativeFiltering::Bilinear})for(unsigned integer:{0u,1u,3u,maximumIntegerScale}){
        prefs={};prefs.scaling=mode;prefs.filtering=filtering;prefs.integerScale=integer;
        const bool smooth=mode!=NativeScaling::Integer&&filtering==NativeFiltering::Bilinear;
        Surface surface(size.first,size.second);display.paint(surface.dc,size.first,size.second,source,prefs);GdiFlush();
        const auto rectangle=displayRectangle(size.first,size.second,prefs);
        unsigned blended=0;
        for(int y=0;y<size.second;++y)for(int x=0;x<size.first;++x){
            const auto colour=surface.pixels[y*size.first+x]&0xffffff;
            const bool inside=x>=rectangle.x&&x<rectangle.x+rectangle.width&&y>=rectangle.y&&y<rectangle.y+rectangle.height;
            if(!inside)require(!colour,"Scaling painted outside letterboxing");
            else if(smooth)blended+=colour!=0xff2020&&colour!=0x2040ff;
            else require(colour==0xff2020||colour==0x2040ff,"Sharp scaling introduced filtered colours");
            if(mode==NativeScaling::Integer&&size.first==800&&inside){const int multiple=rectangle.width/320;const int sx=(x-rectangle.x)/multiple,sy=((2*(y-rectangle.y)+1)*200)/(2*rectangle.height);require(colour==source[sy*320+sx],"Integer scaling changed aspect-corrected nearest-neighbour sampling");}
        }
        if(smooth)require(blended>0,"Smooth scaling did not filter pixel edges");
        require(source==original,"Display scaling modified game pixels");++cases;
    }
    Surface surface(800,500);display.paint(surface.dc,800,500,source,{});GdiFlush();
    const std::vector<std::uint32_t> baseline(surface.pixels,surface.pixels+800*500);
    display.fpsOverlay(surface.dc,800,500,59.9);GdiFlush();
    unsigned fpsPixels=0;
    for(int y=0;y<500;++y)for(int x=0;x<800;++x)if((baseline[y*800+x]&0xffffff)!=(surface.pixels[y*800+x]&0xffffff)){
        require(x<200&&y<63,"FPS overlay escaped the top-left corner");++fpsPixels;
    }
    require(fpsPixels>0&&source==original,"FPS overlay did not compose independently of game pixels");
    display.paint(surface.dc,800,500,source,{});GdiFlush();
    require(std::equal(baseline.begin(),baseline.end(),surface.pixels),"Hiding FPS left pixels on the game surface");
    NativeDisplaySettings settings;settings.openSettings();display.overlay(surface.dc,800,500,settings,L"");GdiFlush();
    require(!std::equal(baseline.begin(),baseline.end(),surface.pixels)&&source==original,"Overlay did not compose separately from game pixels");
    Surface hd(1920,1080);const auto began=std::chrono::steady_clock::now();
    prefs={};prefs.scaling=NativeScaling::Fit;prefs.filtering=NativeFiltering::Bilinear;
    for(unsigned n=0;n<32;++n){source[320*100+160]^=1;display.paint(hd.dc,1920,1080,source,prefs);GdiFlush();}
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
    NativeDisplayPreferences automatic;display.apply(window,automatic);display.beginSettings(window);
    auto fixed=automatic;fixed.integerScale=1;display.apply(window,fixed);
    RECT client{};GetClientRect(window,&client);require(client.right==320&&client.bottom==240,"Fixed 1x did not resize the window client to source dimensions");
    display.apply(window,automatic);display.endSettings(window,false);
    RECT actual{};GetWindowRect(window,&actual);require(EqualRect(&actual,&before),"Cancel did not restore the original window dimensions and position");
    display.beginSettings(window);fixed.integerScale=2;display.apply(window,fixed);display.endSettings(window,true);
    GetClientRect(window,&client);require(client.right==640&&client.bottom==480,"Applying 2x did not retain a 640 by 480 game area");
    SetWindowPos(window,nullptr,120,100,800,500,SWP_NOZORDER|SWP_NOACTIVATE);
    RECT manual{};GetWindowRect(window,&manual);
    auto fullFixed=fixed;fullFixed.fullscreen=true;display.apply(window,fullFixed);display.apply(window,fixed);GetWindowRect(window,&actual);
    require(EqualRect(&actual,&manual),"Fullscreen toggle lost a manually resized fixed-scale window");
    display.beginSettings(window);auto fullscreenPreview=automatic;fullscreenPreview.fullscreen=true;display.apply(window,fullscreenPreview);
    display.apply(window,fixed);display.endSettings(window,false);GetClientRect(window,&client);
    GetWindowRect(window,&actual);require(EqualRect(&actual,&manual)&&!display.isFullscreen(),"Cancel fullscreen preview lost the original fixed-scale window");
    fixed.integerScale=maximumIntegerScale;display.apply(window,fixed);GetClientRect(window,&client);GetWindowRect(window,&actual);
    MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    require(client.right%320==0&&client.bottom%240==0&&client.right/320==client.bottom/240,"Oversized window scale stopped using whole source pixels");
    require(actual.left>=monitor.rcWork.left&&actual.top>=monitor.rcWork.top&&actual.right<=monitor.rcWork.right&&actual.bottom<=monitor.rcWork.bottom,"Oversized fixed window escaped the work area");
    display.beginSettings(window);auto ultra=fixed;ultra.view=NativeView::Ultrawide;ultra.integerScale=1;
    display.apply(window,ultra);GetClientRect(window,&client);
    require(client.right==853&&client.bottom==240,"Ultrawide 1x did not resize to its aspect-corrected client size");
    display.endSettings(window,false);GetWindowRect(window,&before);
    require(EqualRect(&before,&actual),"Cancelling ultrawide preview lost the previous window placement");
    DestroyWindow(window);
}
void settle(NativeGameSession& game){for(unsigned n=0;game.transitioning()&&n<16;++n)game.update(1,{});require(!game.transitioning(),"Race setup fade did not finish");}
void fire(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});game.update(0,{{16,0},0,false});game.update(1,{});settle(game);}
void start(NativeGameSession& game){settle(game);fire(game);fire(game);fire(game);game.update(1,{});settle(game);require(game.page()==NativeMenuPage::Preview,"Display test did not reach preview");game.update(0,{});game.update(0,{{16,0},0,false});settle(game);require(game.page()==NativeMenuPage::Race,"Display test did not enter Practice");}
void leaveRaceStates(const std::filesystem::path& pack){
    for(auto navigation:{NativeMenuNavigation::Original,NativeMenuNavigation::Responsive})
    for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})
    for(unsigned situation=0;situation<4;++situation){
        NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu);start(game);game.setNavigation(navigation);
        const auto matches=[&](){const auto& frame=game.race().frames().back();
            const bool lifting=frame.nearInputs.drawing.raceStartTimer!=0,offTrack=(frame.nearInputs.progress.offTrackFlags&0x80)!=0;
            if(situation==0)return lifting;
            if(situation==1)return !lifting&&!offTrack&&frame.audio.wheelMovement!=0;
            if(situation==2)return !lifting&&offTrack;
            return frame.restarts!=0&&lifting;
        };
        for(unsigned tick=0;!matches()&&tick<1200&&!game.raceEnded();++tick){
            NativeGameInput driving;driving.driveControls=situation==1?1:21;game.update(.12,driving);
        }
        if(!matches())throw std::runtime_error("Leave Race fixture missed situation "+std::to_string(situation));
        const auto ticks=game.race().liveTicks(),restarts=game.race().frames().back().restarts;
        const auto pose=game.race().frames().back().pose.worldPosition;
        game.render(true,false,view);
        NativeDisplaySettings settings;settings.openGameMenu(true);tap(settings,2);tap(settings,2);tap(settings,16);
        require(settings.takeAction()==NativeDisplayAction::LeaveRace&&!settings.blocksGame(),"Leave Race menu action was lost");
        game.releaseInput();NativeGameInput command;command.leaveRace=true;
        const auto result=game.update(0,command);
        require(game.transitioning()||result.raceExited,"Leave Race waited for physics or recovery");
        require(game.race().liveTerminal()&&game.race().frames().back().action==unsigned(scr::RaceTransitionAction::quit),"Leave Race did not select the quit outcome");
        require(game.race().liveTicks()==ticks&&game.race().frames().back().restarts==restarts&&game.race().frames().back().pose.worldPosition==pose,"Leave Race advanced the car before exiting");
        settle(game);require(game.page()==NativeMenuPage::Track,"Leave Race did not restore track selection");
        require(game.memory()[0x1bbb4]==0xc0&&game.memory()[0x57c5b]==0x80&&game.memory()[0x5d724]==0,"Leave Race skipped quit flags or cleanup");
        std::array<std::uint8_t,4> savedSeed{};std::copy_n(game.memory().begin()+0x636b8,4,savedSeed.begin());
        const auto again=game.update(0,{});
        require(!again.raceExited&&game.page()==NativeMenuPage::Track&&game.race().liveTicks()==ticks&&std::equal(savedSeed.begin(),savedSeed.end(),game.memory().begin()+0x636b8),"Leave Race applied its cleanup twice");
    }
}
void leaveRaceOutcomes(const std::filesystem::path& pack){
    const auto seed=startNativeRace(pack,1);
    for(unsigned flags=0;flags<32;++flags){
        auto memory=std::make_unique<NativeMemory>(*seed.memory);
        (*memory)[0x1bbdf]=flags&1?1:0;(*memory)[0x1bb7e]=flags&2?0x80:0;
        (*memory)[0x1bb9c]=flags&4?0x80:0;(*memory)[0x1bbaf]=flags&8?0x80:0;
        (*memory)[0x1bb41]=flags&16?0:0x80;(*memory)[0x1bb6c]=0;
        (*memory)[0x1bbb4]=0x80;
        RaceScene scene;scene.loadNativeState(*memory,seed.payload,{});
        const auto pose=scene.frames().back().pose.worldPosition;
        scene.quitLive();scene.quitLive();
        require(scene.liveTerminal()&&!scene.stepLive(31,true)&&scene.liveTicks()==0&&scene.frames().back().restarts==0&&scene.frames().back().pose.worldPosition==pose,"Host quit ran physics or recovery, or was not idempotent");
        auto actual=std::make_unique<NativeMemory>();scene.copyLiveExit(*actual);
        require((*actual)[0x1bbb4]==(flags&8?0x80:0xc0),"Host quit changed a completed outcome or failed to abandon an unfinished race");
        namespace t=scr::state::transitions;
        if(!(flags&8))(*memory)[0x1bbb4]=0xc0;
        auto state=t::readState(memory->data());(void)scr::finishLocalRaceExit(t::readInputs(memory->data()),state);t::writeState(memory->data(),state);
        require(*actual==*memory,"Host quit changed damage, RNG or state outside the normal quit/cleanup writes");
    }
}
void timing(const std::filesystem::path& pack,const std::filesystem::path& output,WindowsGameDisplay& display){
    NativeGameSession viewed(pack,output/"viewed",NativeGameStart::EstablishedMenu),twin(pack,output/"twin",NativeGameStart::EstablishedMenu);start(viewed);start(twin);
    NativeDisplaySettings settings;Surface surface(800,500);
    for(unsigned tick=0;tick<96;++tick){
        if(tick==32){
            viewed.update(0.06,{});twin.update(0.06,{});const auto before=viewed.race().liveTicks();const auto image=viewed.render(true);
            settings.openSettings();
            for(unsigned n=0;n<120;++n){
                if(n==40){settings.back();settings.openGameMenu(true,0,NativeQuitContext::CareerRace);}
                if(n==80){tap(settings,1);tap(settings,16);require(settings.page()==NativeDisplayPage::QuitConfirmation,"Paused presentation fixture missed quit confirmation");}
                if(!settings.blocksGame())viewed.update(1.0/60,{});
                auto presentation=settings.preferences();presentation.scaling=NativeScaling(n%3);presentation.filtering=NativeFiltering(n%2);presentation.integerScale=n%4;
                presentation.view=NativeView(n%3);
                display.paint(surface.dc,800,500,viewed.render(true,true,presentation.view),presentation);display.overlay(surface.dc,800,500,settings,L"");
                display.fpsOverlay(surface.dc,800,500,60.0);
            }
            require(viewed.race().liveTicks()==before,"Game menu or quit confirmation advanced physics ticks");while(settings.blocksGame())settings.back();
            require(viewed.render(true)==image,"Settings contaminated the underlying race image");
            viewed.update(0.059,{});twin.update(0.059,{});require(viewed.race().liveTicks()==before,"Settings changed the retained sub-tick phase");
            viewed.update(0.001,{});twin.update(0.001,{});require(viewed.race().liveTicks()==before+1,"Resuming lost the original tick boundary");
        }
        viewed.update(0.12,{{},17,false});twin.update(0.12,{{},17,false});
        require(viewed.race().liveTicks()==twin.race().liveTicks(),"Display settings changed later physics scheduling");
    }
    settings.openGameMenu(true);tap(settings,2);tap(settings,2);tap(settings,16);
    const bool leave=settings.takeAction()==NativeDisplayAction::LeaveRace;require(leave&&!settings.blocksGame(),"Practice could not leave via the Game menu");
    NativeGameInput command;command.leaveRace=leave;viewed.update(0,command);twin.update(0,command);settle(viewed);settle(twin);
    require(viewed.page()==NativeMenuPage::Track&&twin.page()==NativeMenuPage::Track,"Display test did not return through Leave Race cleanup");
    require(viewed.memory()==twin.memory(),"Display settings altered final game RAM");
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_display_tests asset-pack output-directory");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    storage(output);navigation();gameMenu();WindowsGameDisplay display;const auto cases=scaling(display);
    widescreenDisplay(display,argv[1],output,NativeView::Widescreen);widescreenDisplay(display,argv[1],output,NativeView::Ultrawide);
    fullscreen(display);leaveRaceStates(argv[1]);leaveRaceOutcomes(argv[1]);timing(argv[1],output,display);
    std::cout<<"{\"status\":\"passed\",\"preference_roundtrips\":384,\"corrupt_preferences\":17,\"legacy_preferences_migrated\":true,\"control_preset_navigation\":true,\"game_menu\":true,\"quit_confirmation\":true,\"leave_race_equivalence\":true,\"atomic_failure_checked\":true,\"controller_navigation\":true,\"scaling_cases\":"<<cases
        <<",\"leave_race_state_cases\":32,\"leave_race_outcome_cases\":32,\"leave_race_without_physics_tick\":true"
        <<",\"fullscreen_roundtrips\":4,\"paused_presentations\":120,\"changing_1080p_filter_ms\":"<<filterMilliseconds
        <<",\"changing_5120x1440_filter_ms\":"<<ultrawideFilterMilliseconds<<",\"wide_aspect_modes\":2,\"ultrawide_car_scale_preserved\":true,\"ultrawide_preference_roundtrip\":true,\"ultrawide_window_cancel\":true,\"original_tick_phase_preserved\":true,\"final_simulation_memory_identical\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include "windows_launcher.h"
#include "native_game_session.h"
#include "windows_drive_input.h"
#include "windows_audio_output.h"
#include "windows_game_display.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
struct Options {
    NativeLaunchPaths paths;
    std::optional<bool> interpolate,fullscreen;
};
Options parse(){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)throw std::runtime_error("Cannot read native game arguments");
    Options options;options.paths=defaultNativeLaunchPaths();
    try{for(int n=1;n<count;++n){
        const std::wstring key=args[n];
        if(key==L"--interpolate"){options.interpolate=true;continue;}
        if(key==L"--fullscreen"){options.fullscreen=true;continue;}
        if(key==L"--windowed"){options.fullscreen=false;continue;}
        if(++n==count)throw std::runtime_error("Missing native game argument");
        if(key==L"--asset-pack")options.paths.pack=args[n];
        else if(key==L"--recordings-dir")options.paths.recordings=args[n];
        else if(key==L"--save-file")options.paths.save=args[n];
        else if(key==L"--settings-file")options.paths.settings=args[n];
        else throw std::runtime_error("Unknown native game argument");
    }}catch(...){LocalFree(args);throw;}
    LocalFree(args);
    return options;
}
class GameWindow {
public:
    explicit GameWindow(const Options& options):launcher_(options.paths,options.fullscreen,options.interpolate),
        settingsFile_(options.paths.settings),settings_(launcher_.preferences()){}
    ~GameWindow(){if(timer_)CloseHandle(timer_);}
    int run(HINSTANCE instance,int show){
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=instance;type.lpfnWndProc=procedure;
        type.lpszClassName=L"StuntCarRacerNativeGame";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassExW(&type);RECT extent{0,0,960,720};AdjustWindowRect(&extent,WS_OVERLAPPEDWINDOW,FALSE);
        auto window=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"Stunt Car Racer",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,extent.right-extent.left,extent.bottom-extent.top,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("Cannot open native game window");
        const BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        launcherView_.create(window);launcherView_.sync(launcher_);
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!timer_)throw std::runtime_error("Cannot create native game timer");
        ShowWindow(window,show);UpdateWindow(window);launcherView_.focusInitial(launcher_);
        auto previous=std::chrono::steady_clock::now(),deadline=previous;
        const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0/60));
        for(;;){
            deadline+=period;
            LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,LONGLONG(std::ceil(std::chrono::duration<double>(deadline-std::chrono::steady_clock::now()).count()*10000000)));
            if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))throw std::runtime_error("Cannot arm native game timer");
            bool tick=false;
            while(!tick){
                const auto wake=MsgWaitForMultipleObjectsEx(1,&timer_,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                if(wake==WAIT_FAILED)throw std::runtime_error("Native game presentation wait failed");
                tick=wake==WAIT_OBJECT_0;
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                    if(message.message==WM_QUIT)return int(message.wParam);
                    if(!game_&&message.message==WM_KEYDOWN&&message.wParam==VK_TAB&&IsDialogMessageW(window,&message))continue;
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
            }
            const auto now=std::chrono::steady_clock::now();
            const double elapsed=std::min(0.25,std::chrono::duration<double>(now-previous).count());previous=now;
            if(deadline<now-period)deadline=now;
            tickGame(window,elapsed);
        }
    }
private:
    NativeLauncher launcher_;
    std::unique_ptr<NativeGameSession> game_;
    WindowsDriveInput controller_;
    PolledDrivePad pad_;
    std::unique_ptr<WindowsAudioOutput> audio_;
    std::array<bool,256> keys_{};
    std::array<bool,256> blockedKeys_{};
    std::vector<NativeRawKeyEvent> rawKeys_;
    std::filesystem::path settingsFile_;
    NativeDisplaySettings settings_;
    WindowsGameDisplay display_;
    WindowsLauncher launcherView_;
    std::wstring settingsError_;
    bool muted_{},focused_{true},suppressInput_{},freshGame_{};
    HANDLE timer_{};
    unsigned frames_{};
    double fps_{};
    std::chrono::steady_clock::time_point fpsStart_=std::chrono::steady_clock::now();
    void launcherAction(HWND window,LauncherAction action){
        if(action==LauncherAction::Quit){DestroyWindow(window);return;}
        if(action==LauncherAction::ChooseAdf){
            std::vector<wchar_t> filename(32768);OPENFILENAMEW chooser{};chooser.lStructSize=sizeof(chooser);chooser.hwndOwner=window;
            chooser.lpstrFilter=L"Amiga disk images (*.adf)\0*.adf\0All files\0*.*\0\0";chooser.lpstrFile=filename.data();chooser.nMaxFile=DWORD(filename.size());
            chooser.lpstrTitle=L"Choose your original Stunt Car Racer ADF";chooser.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&chooser)){launcher_.importDisk(filename.data());launcherView_.sync(launcher_);launcherView_.focusInitial(launcher_);}
            launcherView_.releaseNavigation();return;
        }
        if(action!=LauncherAction::Play)return;
        if(!launcher_.saveForPlay()){launcherView_.sync(launcher_);return;}
        try{
            const auto& paths=launcher_.paths();
            auto started=std::make_unique<NativeGameSession>(paths.pack,paths.recordings,NativeGameStart::OriginalOpening,paths.save);
            started->setNavigation(launcher_.preferences().navigation);
            settings_=NativeDisplaySettings(launcher_.preferences());display_.fullscreen(window,settings_.preferences().fullscreen);
            game_=std::move(started);launcherView_.visible(false);SetFocus(window);clearGameInput();
            for(const unsigned key:{unsigned(VK_RETURN),unsigned(VK_SPACE),unsigned(VK_ESCAPE),unsigned(VK_UP),unsigned(VK_DOWN),unsigned(VK_LEFT),unsigned(VK_RIGHT),unsigned('W'),unsigned('A'),unsigned('S'),unsigned('D')})
                blockedKeys_[key]=(GetAsyncKeyState(int(key))&0x8000)!=0;
            freshGame_=true;InvalidateRect(window,nullptr,FALSE);
        }catch(const std::exception& failure){
            display_.fullscreen(window,false);launcher_.refreshAssets();launcher_.failedStart(failure);launcherView_.visible(true);launcherView_.sync(launcher_);launcherView_.focusInitial(launcher_);
        }
    }
    NativeGameInput input()const{
        NativeGameInput value;
        value.drivePreset=settings_.preferences().controls;
        if(suppressInput_)return value;
        value.rawKeys=rawKeys_;
        const auto& p=pad_.pad;
        value.menu.back=keys_[VK_ESCAPE]||(p.connected&&(p.buttons&0x2000));
        if(game_->usingTextInput()){
            // The original raw keyboard scanner owns name entry. Letters
            // such as WASD and M/I must reach it without host shortcuts.
            if(p.connected)value.menu.controls=std::uint8_t(
                (((p.buttons&0x0001)||p.y>16000)?1:0)|
                (((p.buttons&0x0002)||p.y<-16000)?2:0)|
                (((p.buttons&0x0004)||p.x<-16000)?4:0)|
                (((p.buttons&0x0008)||p.x>16000)?8:0)|((p.buttons&0x1000)?16:0));
            if(settings_.preferences().navigation==NativeMenuNavigation::Responsive)
                value.menu.controls|=std::uint8_t((keys_[VK_UP]?1:0)|(keys_[VK_DOWN]?2:0)|(keys_[VK_LEFT]?4:0)|
                    (keys_[VK_RIGHT]?8:0)|(keys_[VK_RETURN]?16:0));
            value.quit=(p.connected&&(p.buttons&0x0020));return value;
        }
        value.driveControls=mapDriveControls({keys_['W']||keys_[VK_UP],keys_['S']||keys_[VK_DOWN],
            keys_['A']||keys_[VK_LEFT],keys_['D']||keys_[VK_RIGHT],keys_[VK_SPACE]},pad_.pad,settings_.preferences().controls);
        const bool up=keys_[VK_UP]||keys_['W']||(p.connected&&((p.buttons&0x0001)||p.y>16000));
        const bool down=keys_[VK_DOWN]||keys_['S']||(p.connected&&((p.buttons&0x0002)||p.y<-16000));
        const bool left=keys_[VK_LEFT]||keys_['A']||(p.connected&&((p.buttons&0x0004)||p.x<-16000));
        const bool right=keys_[VK_RIGHT]||keys_['D']||(p.connected&&((p.buttons&0x0008)||p.x>16000));
        const bool fire=keys_[VK_RETURN]||keys_[VK_SPACE]||(p.connected&&(p.buttons&0x1000));
        value.menu.controls=std::uint8_t((up?1:0)|(down?2:0)|(left?4:0)|(right?8:0)|(fire?16:0));
        for(unsigned n=0;n<4;++n)if(keys_['1'+n]||keys_[VK_NUMPAD1+n])value.menu.numberedKeys|=std::uint8_t(1u<<n);
        value.quit=keys_[VK_ESCAPE]||(p.connected&&(p.buttons&0x0020));return value;
    }
    void update(HWND window,double elapsed){
        if(!focused_||settings_.blocksGame())return;
        game_->setNavigation(settings_.preferences().navigation);
        const auto changes=game_->update(elapsed,input());
        rawKeys_.clear();
        if(changes.raceStarted){
            if(audio_)audio_->reset(game_->race().audioSeed());
            else audio_=std::make_unique<WindowsAudioOutput>(game_->race().audioSeed());
            audio_->setMuted(muted_);audio_->setPlaying(true);
        }
        if(audio_){
            for(const auto& frame:changes.audio)audio_->submit(frame);
            audio_->setPlaying(game_->page()==NativeMenuPage::Race&&!game_->raceEnded()&&!game_->transitioning());
        }
        if(changes.unavailable!=NativeGameUnavailable::none){
            clearGameInput();
            const auto text=changes.unavailable==NativeGameUnavailable::computerLink?
                L"Computer Link has not been ported yet. Single Player and local Multiplayer are available.":
                L"This route is unavailable in the current game mode.";
            MessageBoxW(window,text,L"Stunt Car Racer — native port",MB_OK|MB_ICONINFORMATION);
        }
    }
    void tickGame(HWND window,double elapsed){
        if(!game_){pad_=controller_.poll();if(focused_)launcherAction(window,launcherView_.navigate(displayPadNavigation(pad_.pad),launcher_));return;}
        if(freshGame_){elapsed=0;freshGame_=false;}
        const bool wasBlocked=settings_.blocksGame();
        const bool wasConnected=pad_.pad.connected;pad_=controller_.poll();
        if(!wasConnected&&pad_.pad.connected)clearGameInput();
        if(pad_.disconnected){
            if(inRace())settings_.suspendRace(navigation());clearGameInput();
        }
        if(focused_&&pad_.pausePressed){
            if(settings_.blocksGame())settings_.back();
            else if(inRace())settings_.pause(navigation());else settings_.openSettings(navigation());
            clearGameInput();syncDisplay(window);
        }else if(focused_&&settings_.blocksGame())navigateOverlay(window);
        // Wait for controls from both presets to release after a settings
        // change, so switching layouts cannot turn a held trigger into gas.
        if(suppressInput_&&std::none_of(blockedKeys_.begin(),blockedKeys_.end(),[](bool down){return down;})&&
            !displayPadNavigation(pad_.pad)&&!mapDriveControls({},pad_.pad))suppressInput_=false;
        if(!wasBlocked)update(window,elapsed);
        if(!IsIconic(window)){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);++frames_;}
        const auto now=std::chrono::steady_clock::now();const auto seconds=std::chrono::duration<double>(now-fpsStart_).count();
        if(seconds>=1){fps_=frames_/seconds;frames_=0;fpsStart_=now;
            std::wostringstream title;title<<L"Stunt Car Racer  |  "<<std::fixed<<std::setprecision(1)<<fps_<<L" FPS";
            if(settings_.blocksGame())title<<L"  |  Paused";
            if(settings_.preferences().interpolate)title<<L"  |  Interpolation";
            title<<L"  |  F10 Settings";
            if(!settingsError_.empty())title<<L"  |  Settings not saved";
            SetWindowTextW(window,title.str().c_str());
        }
    }
    bool inRace()const{return game_&&game_->page()==NativeMenuPage::Race&&!game_->transitioning();}
    bool responsiveMenu()const{return game_&&!inRace()&&settings_.preferences().navigation==NativeMenuNavigation::Responsive;}
    unsigned navigation()const{
        return displayPadNavigation(pad_.pad)|(keys_[VK_UP]?1u:0u)|(keys_[VK_DOWN]?2u:0u)|
            (keys_[VK_LEFT]?4u:0u)|(keys_[VK_RIGHT]?8u:0u)|((keys_[VK_RETURN]||keys_[VK_SPACE])?16u:0u)|(keys_[VK_ESCAPE]?32u:0u);
    }
    void clearGameInput(){
        const auto held=navigation();
        for(unsigned n=0;n<keys_.size();++n)blockedKeys_[n]=blockedKeys_[n]||keys_[n];
        keys_.fill(false);rawKeys_.clear();
        if(game_)game_->releaseInput({std::uint8_t(held&31),0,bool(held&32)});
        suppressInput_=!responsiveMenu();
        if(audio_)audio_->setPlaying(false);
    }
    void syncDisplay(HWND window){if(game_)display_.fullscreen(window,settings_.preferences().fullscreen);InvalidateRect(window,nullptr,FALSE);}
    bool saveSettings(){
        const bool saved=saveDisplayPreferences(settingsFile_,settings_.preferences());
        settingsError_=saved?L"":L"Settings could not be saved. Apply to retry.";return saved;
    }
    void navigateOverlay(HWND window){
        const auto before=settings_.page();
        if(settings_.navigate(navigation())&&saveSettings())settings_.applied();
        if(settings_.page()!=before)clearGameInput();
        syncDisplay(window);
    }
    void paint(HWND window){
        // Keep the last completed image visible while rendering. Clearing the
        // window before game_->render exposed black frames to the compositor.
        const std::vector<std::uint32_t>* pixels=game_?&game_->render(settings_.preferences().interpolate,settings_.blocksGame()):nullptr;
        PAINTSTRUCT painting{};auto target=BeginPaint(window,&painting);RECT client{};GetClientRect(window,&client);
        if(client.right<=0||client.bottom<=0){EndPaint(window,&painting);return;}
        auto dc=CreateCompatibleDC(target);
        auto surface=CreateCompatibleBitmap(target,client.right,client.bottom);
        if(!dc||!surface){
            if(surface)DeleteObject(surface);if(dc)DeleteDC(dc);EndPaint(window,&painting);
            throw std::runtime_error("Cannot allocate native game presentation buffer");
        }
        const auto previous=SelectObject(dc,surface);
        if(pixels){display_.paint(dc,client.right,client.bottom,*pixels,settings_.preferences().scaling);display_.overlay(dc,client.right,client.bottom,settings_,settingsError_);}
        else launcherView_.paint(dc,client.right,client.bottom,launcher_);
        // Background, letterboxing and game pixels become visible together.
        const bool presented=BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY)!=0;
        SelectObject(dc,previous);DeleteObject(surface);DeleteDC(dc);
        EndPaint(window,&painting);
        if(!presented)throw std::runtime_error("Cannot present the native game frame");
    }
    LRESULT message(HWND window,UINT kind,WPARAM key,LPARAM data){
        switch(kind){
        case WM_ERASEBKGND:return 1;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize=game_?POINT{340,240}:POINT{740,640};return 0;
        case WM_DPICHANGED:{const auto suggested=reinterpret_cast<const RECT*>(data);SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        case WM_COMMAND:
            if(!game_&&(HIWORD(key)==BN_CLICKED||HIWORD(key)==CBN_SELCHANGE))launcherAction(window,launcherView_.command(LOWORD(key),launcher_));return 0;
        case WM_DRAWITEM:if(!game_){launcherView_.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(data));return TRUE;}break;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
            if(!game_)return reinterpret_cast<LRESULT>(launcherView_.controlColour(reinterpret_cast<HDC>(key)));break;
        case WindowsLauncher::navigationMessage:
            if(!game_)launcherAction(window,launcherView_.navigate(unsigned(key),launcher_));return 0;
        case WM_ACTIVATEAPP:
            focused_=key!=0;
            if(!focused_){if(inRace())settings_.suspendRace();clearGameInput();blockedKeys_.fill(false);settings_.releaseNavigation();launcherView_.releaseNavigation();}
            else if(responsiveMenu())for(unsigned n=0;n<blockedKeys_.size();++n)blockedKeys_[n]=(GetAsyncKeyState(int(n))&0x8000)!=0;
            return 0;
        case WM_SYSKEYDOWN:
            if(key==VK_F10||key==VK_F11)return message(window,WM_KEYDOWN,key,data);
            return DefWindowProcW(window,kind,key,data);
        case WM_SYSKEYUP:
            if(key==VK_F10||key==VK_F11)return message(window,WM_KEYUP,key,data);
            return DefWindowProcW(window,kind,key,data);
        case WM_KEYDOWN:
            if(!game_){if(!(data&0x40000000)){const unsigned nav=key==VK_UP?1:key==VK_DOWN?2:key==VK_LEFT?4:key==VK_RIGHT?8:(key==VK_RETURN||key==VK_SPACE)?16:key==VK_ESCAPE?32:0;
                launcherAction(window,launcherView_.navigate(nav,launcher_));}return 0;}
            if(key==VK_F10||key==VK_F11){
                if(!(data&0x40000000)){
                    if(key==VK_F10){
                        if(settings_.page()==NativeDisplayPage::Settings)settings_.back();else settings_.openSettings(navigation());
                        clearGameInput();
                    }else{settings_.toggleFullscreen();if(settings_.page()!=NativeDisplayPage::Settings)saveSettings();}
                    syncDisplay(window);
                }
                return 0;
            }
            if(settings_.blocksGame()&&(data&0x40000000))return 0;
            if(responsiveMenu()&&key<blockedKeys_.size()&&blockedKeys_[key])return 0;
            if(key<keys_.size())keys_[key]=true;
            if(settings_.blocksGame()){
                if(!(data&0x40000000)){
                    if(key=='P'&&settings_.page()==NativeDisplayPage::Pause){settings_.back();clearGameInput();}
                    else navigateOverlay(window);
                }
                return 0;
            }
            if(key=='P'&&inRace()&&!(data&0x40000000)){settings_.pause(navigation());clearGameInput();InvalidateRect(window,nullptr,FALSE);return 0;}
            if(suppressInput_){if(key<blockedKeys_.size())blockedKeys_[key]=true;return 0;}
            if(!(data&0x40000000)){
                if(const auto raw=nativeAmigaRawKey(unsigned(key),(unsigned(data)>>16)&255,(data&0x01000000)!=0))rawKeys_.push_back({*raw,true});
                if(!game_->usingTextInput()&&key=='M'){muted_=!muted_;if(audio_)audio_->setMuted(muted_);}
                else if(!game_->usingTextInput()&&key=='I'){settings_.toggleInterpolation();saveSettings();}
            }
            update(window,0);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_KEYUP:
            if(!game_){launcherView_.navigate(0,launcher_);return 0;}
            if(key<keys_.size())keys_[key]=false;
            if(key<blockedKeys_.size())blockedKeys_[key]=false;
            if(settings_.blocksGame()){navigateOverlay(window);return 0;}
            if(suppressInput_||key==VK_F10||key==VK_F11)return 0;
            if(const auto raw=nativeAmigaRawKey(unsigned(key),(unsigned(data)>>16)&255,(data&0x01000000)!=0))rawKeys_.push_back({*raw,false});
            update(window,0);return 0;
        case WM_SIZE:if(!game_)launcherView_.layout(window);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_DISPLAYCHANGE:display_.monitorChanged(window);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_PAINT:paint(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        default:return DefWindowProcW(window,kind,key,data);
        }
        return DefWindowProcW(window,kind,key,data);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT kind,WPARAM key,LPARAM data){
        auto self=reinterpret_cast<GameWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(kind==WM_NCCREATE){self=static_cast<GameWindow*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,kind,key,data);
        try{return self->message(window,kind,key,data);}
        catch(const std::exception& error){MessageBoxA(window,error.what(),"Native game failed",MB_OK|MB_ICONERROR);DestroyWindow(window);return 0;}
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    try{const auto options=parse();if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))SetProcessDPIAware();GameWindow game(options);return game.run(instance,show);}
    catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"Stunt Car Racer",MB_OK|MB_ICONERROR);return 1;}
}

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
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
    std::filesystem::path pack,recordings=L"analysis/live-driving",saveFile=L"working/saves/stunt-car-racer.scr-save";
    std::filesystem::path settingsFile=L"working/settings/display.ini";
    std::optional<bool> interpolate,fullscreen;
};
Options parse(){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)throw std::runtime_error("Cannot read native game arguments");
    Options options;
    try{for(int n=1;n<count;++n){
        const std::wstring key=args[n];
        if(key==L"--interpolate"){options.interpolate=true;continue;}
        if(key==L"--fullscreen"){options.fullscreen=true;continue;}
        if(key==L"--windowed"){options.fullscreen=false;continue;}
        if(++n==count)throw std::runtime_error("Missing native game argument");
        if(key==L"--asset-pack")options.pack=args[n];
        else if(key==L"--recordings-dir")options.recordings=args[n];
        else if(key==L"--save-file")options.saveFile=args[n];
        else if(key==L"--settings-file")options.settingsFile=args[n];
        else throw std::runtime_error("Unknown native game argument");
    }}catch(...){LocalFree(args);throw;}
    LocalFree(args);if(options.pack.empty())throw std::runtime_error("Supply --asset-pack with your imported local data pack");
    return options;
}
NativeDisplayPreferences displayPreferences(const Options& options){
    auto preferences=loadDisplayPreferences(options.settingsFile);
    if(options.interpolate)preferences.interpolate=*options.interpolate;
    if(options.fullscreen)preferences.fullscreen=*options.fullscreen;
    return preferences;
}
class GameWindow {
public:
    explicit GameWindow(const Options& options):game_(options.pack,options.recordings,NativeGameStart::OriginalOpening,options.saveFile),
        settingsFile_(options.settingsFile),settings_(displayPreferences(options)){}
    ~GameWindow(){if(timer_)CloseHandle(timer_);}
    int run(HINSTANCE instance,int show){
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=instance;type.lpfnWndProc=procedure;
        type.lpszClassName=L"StuntCarRacerNativeGame";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassExW(&type);RECT extent{0,0,960,600};AdjustWindowRect(&extent,WS_OVERLAPPEDWINDOW,FALSE);
        auto window=CreateWindowExW(0,type.lpszClassName,L"Stunt Car Racer",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,extent.right-extent.left,extent.bottom-extent.top,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("Cannot open native game window");
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!timer_)throw std::runtime_error("Cannot create native game timer");
        ShowWindow(window,show);display_.fullscreen(window,settings_.preferences().fullscreen);UpdateWindow(window);
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
    NativeGameSession game_;
    WindowsDriveInput controller_;
    PolledDrivePad pad_;
    std::unique_ptr<WindowsAudioOutput> audio_;
    std::array<bool,256> keys_{};
    std::array<bool,256> blockedKeys_{};
    std::vector<NativeRawKeyEvent> rawKeys_;
    std::filesystem::path settingsFile_;
    NativeDisplaySettings settings_;
    WindowsGameDisplay display_;
    std::wstring settingsError_;
    bool muted_{},focused_{true},suppressInput_{};
    HANDLE timer_{};
    unsigned frames_{};
    double fps_{};
    std::chrono::steady_clock::time_point fpsStart_=std::chrono::steady_clock::now();
    NativeGameInput input()const{
        NativeGameInput value;
        if(suppressInput_)return value;
        value.rawKeys=rawKeys_;
        const auto& p=pad_.pad;
        if(game_.usingTextInput()){
            // The original raw keyboard scanner owns name entry. Letters
            // such as WASD and M/I must reach it without host shortcuts.
            if(p.connected)value.menu.controls=std::uint8_t(
                (((p.buttons&0x0001)||p.y>16000)?1:0)|
                (((p.buttons&0x0002)||p.y<-16000)?2:0)|
                (((p.buttons&0x0004)||p.x<-16000)?4:0)|
                (((p.buttons&0x0008)||p.x>16000)?8:0)|((p.buttons&0x1000)?16:0));
            value.quit=(p.connected&&(p.buttons&0x0020));return value;
        }
        value.driveControls=mapDriveControls({keys_['W']||keys_[VK_UP],keys_['S']||keys_[VK_DOWN],
            keys_['A']||keys_[VK_LEFT],keys_['D']||keys_[VK_RIGHT],keys_[VK_SPACE]},pad_.pad);
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
        const auto changes=game_.update(elapsed,input());
        rawKeys_.clear();
        if(changes.raceStarted){
            if(audio_)audio_->reset(game_.race().audioSeed());
            else audio_=std::make_unique<WindowsAudioOutput>(game_.race().audioSeed());
            audio_->setMuted(muted_);audio_->setPlaying(true);
        }
        if(audio_){
            for(const auto& frame:changes.audio)audio_->submit(frame);
            audio_->setPlaying(game_.page()==NativeMenuPage::Race&&!game_.raceEnded()&&!game_.transitioning());
        }
        if(changes.unavailable!=NativeGameUnavailable::none){
            keys_.fill(false);rawKeys_.clear();game_.releaseInput();
            const auto text=changes.unavailable==NativeGameUnavailable::computerLink?
                L"Computer Link has not been ported yet. Single Player and local Multiplayer are available.":
                L"This route is unavailable in the current game mode.";
            MessageBoxW(window,text,L"Stunt Car Racer — native port",MB_OK|MB_ICONINFORMATION);
        }
    }
    void tickGame(HWND window,double elapsed){
        const bool wasBlocked=settings_.blocksGame();
        pad_=controller_.poll();
        if(pad_.disconnected){
            if(inRace())settings_.suspendRace(navigation());clearGameInput();
        }
        if(focused_&&pad_.pausePressed){
            if(settings_.blocksGame())settings_.back();
            else if(inRace())settings_.pause(navigation());else settings_.openSettings(navigation());
            clearGameInput();syncDisplay(window);
        }else if(focused_&&settings_.blocksGame())navigateOverlay(window);
        if(suppressInput_&&std::none_of(blockedKeys_.begin(),blockedKeys_.end(),[](bool down){return down;})&&
            !displayPadNavigation(pad_.pad)&&!mapDriveControls({},pad_.pad))suppressInput_=false;
        if(!wasBlocked)update(window,elapsed);
        if(!IsIconic(window)){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);++frames_;}
        const auto now=std::chrono::steady_clock::now();const auto seconds=std::chrono::duration<double>(now-fpsStart_).count();
        if(seconds>=1){fps_=frames_/seconds;frames_=0;fpsStart_=now;
            std::wostringstream title;title<<L"Stunt Car Racer  |  "<<std::fixed<<std::setprecision(1)<<fps_<<L" FPS";
            if(settings_.blocksGame())title<<L"  |  Paused";
            if(settings_.preferences().interpolate)title<<L"  |  Interpolation";
            title<<L"  |  F10 Display";
            if(!settingsError_.empty())title<<L"  |  Settings not saved";
            SetWindowTextW(window,title.str().c_str());
        }
    }
    bool inRace()const{return game_.page()==NativeMenuPage::Race&&!game_.transitioning();}
    unsigned navigation()const{
        return displayPadNavigation(pad_.pad)|(keys_[VK_UP]?1u:0u)|(keys_[VK_DOWN]?2u:0u)|
            (keys_[VK_LEFT]?4u:0u)|(keys_[VK_RIGHT]?8u:0u)|((keys_[VK_RETURN]||keys_[VK_SPACE])?16u:0u)|(keys_[VK_ESCAPE]?32u:0u);
    }
    void clearGameInput(){
        for(unsigned n=0;n<keys_.size();++n)blockedKeys_[n]=blockedKeys_[n]||keys_[n];
        keys_.fill(false);rawKeys_.clear();game_.releaseInput();suppressInput_=true;
        if(audio_)audio_->setPlaying(false);
    }
    void syncDisplay(HWND window){display_.fullscreen(window,settings_.preferences().fullscreen);InvalidateRect(window,nullptr,FALSE);}
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
        // window before game_.render exposed black frames to the compositor.
        const auto& pixels=game_.render(settings_.preferences().interpolate,settings_.blocksGame());
        PAINTSTRUCT painting{};auto target=BeginPaint(window,&painting);RECT client{};GetClientRect(window,&client);
        if(client.right<=0||client.bottom<=0){EndPaint(window,&painting);return;}
        auto dc=CreateCompatibleDC(target);
        auto surface=CreateCompatibleBitmap(target,client.right,client.bottom);
        if(!dc||!surface){
            if(surface)DeleteObject(surface);if(dc)DeleteDC(dc);EndPaint(window,&painting);
            throw std::runtime_error("Cannot allocate native game presentation buffer");
        }
        const auto previous=SelectObject(dc,surface);
        display_.paint(dc,client.right,client.bottom,pixels,settings_.preferences().scaling);
        display_.overlay(dc,client.right,client.bottom,settings_,settingsError_);
        // Background, letterboxing and game pixels become visible together.
        const bool presented=BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY)!=0;
        SelectObject(dc,previous);DeleteObject(surface);DeleteDC(dc);
        EndPaint(window,&painting);
        if(!presented)throw std::runtime_error("Cannot present the native game frame");
    }
    LRESULT message(HWND window,UINT kind,WPARAM key,LPARAM data){
        switch(kind){
        case WM_ERASEBKGND:return 1;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize={340,240};return 0;
        case WM_ACTIVATEAPP:
            focused_=key!=0;
            if(!focused_){if(inRace())settings_.suspendRace();clearGameInput();blockedKeys_.fill(false);settings_.releaseNavigation();}
            return 0;
        case WM_SYSKEYDOWN:
            if(key==VK_F10||key==VK_F11)return message(window,WM_KEYDOWN,key,data);
            return DefWindowProcW(window,kind,key,data);
        case WM_SYSKEYUP:
            if(key==VK_F10||key==VK_F11)return message(window,WM_KEYUP,key,data);
            return DefWindowProcW(window,kind,key,data);
        case WM_KEYDOWN:
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
                if(!game_.usingTextInput()&&key=='M'){muted_=!muted_;if(audio_)audio_->setMuted(muted_);}
                else if(!game_.usingTextInput()&&key=='I'){settings_.toggleInterpolation();saveSettings();}
            }
            update(window,0);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_KEYUP:
            if(key<keys_.size())keys_[key]=false;
            if(key<blockedKeys_.size())blockedKeys_[key]=false;
            if(settings_.blocksGame()){navigateOverlay(window);return 0;}
            if(suppressInput_||key==VK_F10||key==VK_F11)return 0;
            if(const auto raw=nativeAmigaRawKey(unsigned(key),(unsigned(data)>>16)&255,(data&0x01000000)!=0))rawKeys_.push_back({*raw,false});
            update(window,0);return 0;
        case WM_SIZE:InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_DISPLAYCHANGE:display_.monitorChanged(window);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_PAINT:paint(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        default:return DefWindowProcW(window,kind,key,data);
        }
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
    try{const auto options=parse();SetProcessDPIAware();GameWindow game(options);return game.run(instance,show);}
    catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"Stunt Car Racer",MB_OK|MB_ICONERROR);return 1;}
}

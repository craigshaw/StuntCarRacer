#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "native_game_session.h"
#include "windows_drive_input.h"
#include "windows_audio_output.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
struct Options {
    std::filesystem::path pack,recordings=L"analysis/live-driving";
    bool interpolate{};
};
Options parse(){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)throw std::runtime_error("Cannot read native game arguments");
    Options options;
    try{for(int n=1;n<count;++n){
        const std::wstring key=args[n];
        if(key==L"--interpolate"){options.interpolate=true;continue;}
        if(++n==count)throw std::runtime_error("Missing native game argument");
        if(key==L"--asset-pack")options.pack=args[n];
        else if(key==L"--recordings-dir")options.recordings=args[n];
        else throw std::runtime_error("Unknown native game argument");
    }}catch(...){LocalFree(args);throw;}
    LocalFree(args);if(options.pack.empty())throw std::runtime_error("Supply --asset-pack with your imported local data pack");
    return options;
}
class GameWindow {
public:
    explicit GameWindow(const Options& options):game_(options.pack,options.recordings),interpolate_(options.interpolate){}
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
        ShowWindow(window,show);UpdateWindow(window);
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
    bool paused_{},muted_{},interpolate_{},focused_{true};
    HANDLE timer_{};
    unsigned frames_{};
    double fps_{};
    std::chrono::steady_clock::time_point fpsStart_=std::chrono::steady_clock::now();
    NativeGameInput input()const{
        NativeGameInput value;
        value.driveControls=mapDriveControls({keys_['W']||keys_[VK_UP],keys_['S']||keys_[VK_DOWN],
            keys_['A']||keys_[VK_LEFT],keys_['D']||keys_[VK_RIGHT],keys_[VK_SPACE]},pad_.pad);
        const auto& p=pad_.pad;
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
        if(!focused_||paused_)return;
        const auto changes=game_.update(elapsed,input());
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
            keys_.fill(false);game_.releaseInput();
            const auto text=changes.unavailable==NativeGameUnavailable::movingDrawbridge?
                L"The moving Draw Bridge track is not yet supported by the native physics. Choose another track.":
                L"This menu route has not been ported yet. The Practise route is available.";
            MessageBoxW(window,text,L"Stunt Car Racer — native port",MB_OK|MB_ICONINFORMATION);
        }
    }
    void tickGame(HWND window,double elapsed){
        pad_=controller_.poll();
        if(pad_.disconnected){
            paused_=game_.page()==NativeMenuPage::Race&&!game_.transitioning();keys_.fill(false);game_.releaseInput();
            if(audio_)audio_->setPlaying(false);
        }
        if(focused_&&pad_.pausePressed&&game_.page()==NativeMenuPage::Race&&!game_.transitioning())togglePause();
        update(window,elapsed);
        if(!IsIconic(window)){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);++frames_;}
        const auto now=std::chrono::steady_clock::now();const auto seconds=std::chrono::duration<double>(now-fpsStart_).count();
        if(seconds>=1){fps_=frames_/seconds;frames_=0;fpsStart_=now;
            std::wostringstream title;title<<L"Stunt Car Racer  |  "<<std::fixed<<std::setprecision(1)<<fps_<<L" FPS";
            if(paused_)title<<L"  |  Paused";
            if(interpolate_)title<<L"  |  Interpolation";
            SetWindowTextW(window,title.str().c_str());
        }
    }
    void togglePause(){paused_=!paused_;keys_.fill(false);game_.releaseInput();if(audio_)audio_->setPlaying(!paused_);}
    void paint(HWND window){
        // Keep the last completed image visible while rendering. Clearing the
        // window before game_.render exposed black frames to the compositor.
        const auto& pixels=game_.render(interpolate_,paused_);
        PAINTSTRUCT painting{};auto target=BeginPaint(window,&painting);RECT client{};GetClientRect(window,&client);
        if(client.right<=0||client.bottom<=0){EndPaint(window,&painting);return;}
        auto dc=CreateCompatibleDC(target);
        auto surface=CreateCompatibleBitmap(target,client.right,client.bottom);
        if(!dc||!surface){
            if(surface)DeleteObject(surface);if(dc)DeleteDC(dc);EndPaint(window,&painting);
            throw std::runtime_error("Cannot allocate native game presentation buffer");
        }
        const auto previous=SelectObject(dc,surface);
        FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const int scale=std::max(1,int(std::min(client.right/320,client.bottom/200)));
        const int width=scale*320,height=scale*200,x=(client.right-width)/2,y=(client.bottom-height)/2;
        BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=320;
        bitmap.bmiHeader.biHeight=-200;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(dc,COLORONCOLOR);
        const auto drawn=StretchDIBits(dc,x,y,width,height,0,0,320,200,pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
        // Background, letterboxing and game pixels become visible together.
        const bool presented=drawn!=GDI_ERROR&&drawn!=0&&BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
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
            if(!focused_){keys_.fill(false);game_.releaseInput();if(game_.page()==NativeMenuPage::Race&&!game_.transitioning())paused_=true;if(audio_)audio_->setPlaying(false);}
            return 0;
        case WM_KEYDOWN:
            if(key<keys_.size())keys_[key]=true;
            if(!(data&0x40000000)){
                if(key=='P'&&game_.page()==NativeMenuPage::Race&&!game_.transitioning())togglePause();
                else if(key=='M'){muted_=!muted_;if(audio_)audio_->setMuted(muted_);}
                else if(key=='I')interpolate_=!interpolate_;
            }
            update(window,0);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_KEYUP:if(key<keys_.size())keys_[key]=false;update(window,0);return 0;
        case WM_SIZE:InvalidateRect(window,nullptr,FALSE);return 0;
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

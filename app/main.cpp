#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include "platform/windows/launcher.h"
#include "session/native_game_session.h"
#include "platform/windows/drive_input.h"
#include "platform/windows/audio_output.h"
#include "platform/windows/game_display.h"
#include "platform/windows/player_data_ownership.h"
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
using namespace scr::runtime;
NativeLaunchOptions parse(){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args)throw std::runtime_error("Cannot read native game arguments");
    try{
        std::vector<std::wstring> arguments;for(int n=1;n<count;++n)arguments.emplace_back(args[n]);
        auto options=parseNativeLaunchOptions(defaultNativeLaunchPaths(),arguments);LocalFree(args);return options;
    }catch(...){LocalFree(args);throw;}
}
struct WindowPaint {
    HWND window;
    PAINTSTRUCT state{};
    HDC target{BeginPaint(window,&state)};
    ~WindowPaint(){if(target)EndPaint(window,&state);}
};
class GameWindow {
public:
    GameWindow(const NativeLaunchOptions& options,std::wstring instanceName):launcher_(options.paths,options.fullscreen,options.interpolate),
        settingsFile_(options.paths.settings),settings_(launcher_.preferences()),dataLayout_(options.dataLayout),instanceName_(std::move(instanceName)){
    }
    ~GameWindow(){game_.reset();if(timer_)CloseHandle(timer_);}
    int run(HINSTANCE instance,int show){
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=instance;type.lpfnWndProc=procedure;
        type.lpszClassName=instanceName_.c_str();type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
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
    WindowsBackBuffer backBuffer_;
    HMONITOR bufferMonitor_{};
    WindowsLauncher launcherView_;
    std::wstring settingsError_;
    bool muted_{},focused_{true},suppressInput_{},freshGame_{},showFps_{},displayEditing_{};
    bool leaveRaceRequested_{},closeFromMenu_{};
    PlayerDataLayout dataLayout_;
    HANDLE timer_{};
    std::wstring instanceName_;
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
            auto started=std::make_unique<NativeGameSession>(paths.pack,paths.recordings,NativeGameStart::OriginalOpening);
            started->enableCareers(paths.dataRoot,dataLayout_.originalHall,dataLayout_.enhancedHall,paths.settings);
            started->enableMultiplayerGames(paths.settings);
            started->setNavigation(launcher_.preferences().navigation);
            started->setTimingCategory(launcher_.preferences().timing);
            settings_=NativeDisplaySettings(launcher_.preferences());
            game_=std::move(started);launcherView_.visible(false);display_.apply(window,settings_.preferences());SetFocus(window);clearGameInput();
            for(const unsigned key:{unsigned(VK_RETURN),unsigned(VK_SPACE),unsigned(VK_ESCAPE),unsigned(VK_UP),unsigned(VK_DOWN),unsigned(VK_LEFT),unsigned(VK_RIGHT),unsigned('W'),unsigned('A'),unsigned('S'),unsigned('D')})
                blockedKeys_[key]=(GetAsyncKeyState(int(key))&0x8000)!=0;
            frames_=0;fps_=0;fpsStart_=std::chrono::steady_clock::now();
            freshGame_=true;InvalidateRect(window,nullptr,FALSE);
        }catch(const std::exception& failure){
            game_.reset();display_.fullscreen(window,false);launcher_.refreshAssets();launcher_.failedStart(failure);launcherView_.visible(true);launcherView_.layout(window);launcherView_.sync(launcher_);launcherView_.focusInitial(launcher_);
        }
    }
    bool canLeaveRace()const{return inRace()&&!game_->raceEnded();}
    bool typingName()const{return game_->enteringName()||game_->savePhase()==NativeSaveMenuPhase::SaveName;}
    NativeQuitContext quitContext()const{
        if(game_->multiplayer())return NativeQuitContext::Multiplayer;
        if(game_->activeCareer())return canLeaveRace()&&game_->competitive()?NativeQuitContext::CareerRace:NativeQuitContext::Career;
        return NativeQuitContext::General;
    }
    void openGameMenu(HWND window){
        syncGhostMenu();
        settings_.openGameMenu(canLeaveRace(),navigation(),quitContext());clearGameInput();syncDisplay(window);
    }
    void toggleGameMenu(HWND window){
        if(settings_.blocksGame()){settings_.back();clearGameInput();syncDisplay(window);}
        else openGameMenu(window);
    }
    void closeApplication(HWND window){
        if(!game_||game_->requestClose()){DestroyWindow(window);return;}
        closeFromMenu_=game_->closePending();
        while(settings_.blocksGame())settings_.back();
        clearGameInput();syncDisplay(window);
    }
    NativeGameInput input()const{
        NativeGameInput value;
        value.leaveRace=leaveRaceRequested_;
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
        value.quit=(p.connected&&(p.buttons&0x0020));return value;
    }
    void update(HWND window,double elapsed){
        const bool active=focused_&&!settings_.blocksGame();
        if(!active)return;
        if(active){game_->setNavigation(settings_.preferences().navigation);game_->setTimingCategory(settings_.preferences().timing);}
        const auto changes=game_->update(active?elapsed:0,active?input():NativeGameInput{});
        if(game_->closeReady()){DestroyWindow(window);return;}
        if(closeFromMenu_&&!game_->closePending()){closeFromMenu_=false;openGameMenu(window);return;}
        if(changes.raceStarted||changes.raceExited||game_->raceEnded())leaveRaceRequested_=false;
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
            const auto text=L"This route is unavailable in the current game mode.";
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
            if(inRace())settings_.suspendRace(navigation(),canLeaveRace(),quitContext());clearGameInput();
        }
        if(focused_&&pad_.pausePressed){
            toggleGameMenu(window);
        }else if(focused_&&settings_.blocksGame())navigateOverlay(window);
        if(!IsWindow(window))return;
        // Wait for controls from both presets to release after a settings
        // change, so switching layouts cannot turn a held trigger into gas.
        if(suppressInput_&&std::none_of(blockedKeys_.begin(),blockedKeys_.end(),[](bool down){return down;})&&
            !displayPadNavigation(pad_.pad)&&!mapDriveControls({},pad_.pad))suppressInput_=false;
        if(!wasBlocked)update(window,elapsed);
        if(!IsIconic(window)){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);}
        const auto now=std::chrono::steady_clock::now();const auto seconds=std::chrono::duration<double>(now-fpsStart_).count();
        if(seconds>=1){fps_=frames_/seconds;frames_=0;fpsStart_=now;
            std::wostringstream title;title<<L"Stunt Car Racer  |  "<<std::fixed<<std::setprecision(1)<<fps_<<L" FPS";
            if(settings_.blocksGame())title<<L"  |  Paused";
            if(settings_.preferences().interpolate)title<<L"  |  Interpolation";
            const bool escapeMenu=game_->page()==NativeMenuPage::Race||(game_->openingActive()&&game_->openingPage()==NativeOpeningPage::Mode);
            title<<(escapeMenu?L"  |  Esc / Start Game menu":L"  |  P / Start Game menu")<<L"  |  F10 Settings";
            if(!settingsError_.empty())title<<L"  |  Settings not saved";
            if(game_->page()!=NativeMenuPage::Race||settings_.blocksGame()){
                const auto status=game_->saveStatus();title<<L"  |  "<<std::wstring(status.begin(),status.end());
            }
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
    void syncDisplay(HWND window,bool applied=false){
        if(game_){
            game_->setTimingCategory(settings_.preferences().timing);
            const bool editing=settings_.page()==NativeDisplayPage::Settings;
            if(editing&&!displayEditing_)display_.beginSettings(window);
            display_.apply(window,settings_.preferences());
            if(!editing&&displayEditing_)display_.endSettings(window,applied);
            displayEditing_=editing;
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    bool saveSettings(){
        const bool saved=saveDisplayPreferences(settingsFile_,settings_.preferences());
        settingsError_=saved?L"":L"Settings could not be saved. Apply to retry.";return saved;
    }
    void navigateOverlay(HWND window){
        syncGhostMenu();
        const auto before=settings_.page();
        const bool applied=settings_.navigate(navigation())&&saveSettings();
        if(applied)settings_.applied();
        if(settings_.page()!=before)clearGameInput();
        syncDisplay(window,applied);
        const auto action=settings_.takeAction();
        if(action==NativeDisplayAction::LeaveRace){leaveRaceRequested_=true;clearGameInput();}
        else if(action==NativeDisplayAction::Quit)closeApplication(window);
        else if(action==NativeDisplayAction::ClearGhost){game_->clearPracticeGhost();syncGhostMenu();settings_.releaseNavigation();clearGameInput();}
    }
    void syncGhostMenu(){
        if(!game_)return;const auto time=game_->practiceGhostTime();
        const auto target=game_->practiceGhostTarget();
        settings_.setPracticeGhostContext(game_->practiceGhostActive(),std::wstring(time.begin(),time.end()),game_->canClearPracticeGhost(),std::wstring(target.begin(),target.end()),game_->effectiveTimingCategory());
    }
    void paint(HWND window){
        // Keep the last completed image visible while rendering. Clearing the
        // window before game_->render exposed black frames to the compositor.
        if(game_){game_->setPracticeGhostVisible(settings_.preferences().practiceGhost);game_->setTimingCategory(settings_.preferences().timing);}
        const std::vector<std::uint32_t>* pixels=game_?&game_->render(settings_.preferences().interpolate,settings_.blocksGame(),settings_.preferences().view):nullptr;
        syncGhostMenu();
        {
            const WindowPaint painting{window};
            if(!painting.target)throw std::runtime_error("Cannot begin native game paint");
            RECT client{};GetClientRect(window,&client);
            if(client.right<=0||client.bottom<=0)return;
            backBuffer_.paint(painting.target,client.right,client.bottom,[&](HDC dc){
                if(pixels){
                    display_.paint(dc,client.right,client.bottom,*pixels,settings_.preferences());
                    display_.overlay(dc,client.right,client.bottom,settings_,settingsError_);
                    if(settings_.page()!=NativeDisplayPage::Settings){const auto feedback=game_->ghostFeedback();display_.ghostFeedback(dc,client.right,client.bottom,std::wstring(feedback.begin(),feedback.end()),game_->ghostFeedbackWarning());}
                    if(showFps_)display_.fpsOverlay(dc,client.right,client.bottom,fps_);
                }
                else launcherView_.paint(dc,client.right,client.bottom,launcher_);
            });
        }
        if(pixels&&!IsIconic(window))++frames_;
    }
    LRESULT message(HWND window,UINT kind,WPARAM key,LPARAM data){
        switch(kind){
        case WM_ERASEBKGND:return 1;
        case WM_GETMINMAXINFO:{
            POINT minimum{740,640};
            if(game_){RECT client{0,0,320,originalDisplayHeight};AdjustWindowRectExForDpi(&client,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window));minimum={client.right-client.left,client.bottom-client.top};}
            reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize=minimum;return 0;
        }
        case WM_DPICHANGED:{
            backBuffer_.reset();
            const auto suggested=reinterpret_cast<const RECT*>(data);SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);if(game_)display_.apply(window,settings_.preferences(),true);return 0;
        }
        case WM_COMMAND:
            if(!game_&&(HIWORD(key)==BN_CLICKED||HIWORD(key)==CBN_SELCHANGE))launcherAction(window,launcherView_.command(LOWORD(key),launcher_));return 0;
        case WM_DRAWITEM:if(!game_){launcherView_.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(data));return TRUE;}break;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
            if(!game_)return reinterpret_cast<LRESULT>(launcherView_.controlColour(reinterpret_cast<HDC>(key)));break;
        case WindowsLauncher::navigationMessage:
            if(!game_)launcherAction(window,launcherView_.navigate(unsigned(key),launcher_));return 0;
        case WM_ACTIVATEAPP:
            focused_=key!=0;
            if(!focused_){if(inRace())settings_.suspendRace(0,canLeaveRace(),quitContext());clearGameInput();blockedKeys_.fill(false);settings_.releaseNavigation();launcherView_.releaseNavigation();}
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
                        if(settings_.page()==NativeDisplayPage::QuitConfirmation||settings_.page()==NativeDisplayPage::ClearGhostConfirmation)return 0;
                        if(settings_.page()==NativeDisplayPage::Settings)settings_.back();else settings_.openSettings(navigation());
                        clearGameInput();
                    }else{settings_.toggleFullscreen();if(settings_.page()!=NativeDisplayPage::Settings)saveSettings();}
                    syncDisplay(window);
                }
                return 0;
            }
            if(key=='F'&&!game_->usingTextInput()){
                if(!(data&0x40000000)){showFps_=!showFps_;InvalidateRect(window,nullptr,FALSE);}
                return 0;
            }
            if(settings_.blocksGame()&&(data&0x40000000))return 0;
            if(responsiveMenu()&&key<blockedKeys_.size()&&blockedKeys_[key])return 0;
            if(key<keys_.size())keys_[key]=true;
            if(settings_.blocksGame()){
                if(!(data&0x40000000)){
                    if(key=='P')toggleGameMenu(window);
                    else navigateOverlay(window);
                }
                return 0;
            }
            if(gameMenuShortcut(unsigned(key),typingName(),game_->page()==NativeMenuPage::Race,
                game_->openingActive()&&game_->openingPage()==NativeOpeningPage::Mode)){
                if(!(data&0x40000000))openGameMenu(window);return 0;
            }
            if(suppressInput_){if(key<blockedKeys_.size())blockedKeys_[key]=true;return 0;}
            if(!(data&0x40000000)){
                if(const auto raw=nativeAmigaRawKey(unsigned(key),(unsigned(data)>>16)&255,(data&0x01000000)!=0))rawKeys_.push_back({*raw,true});
                if(!game_->usingTextInput()&&key=='M'){muted_=!muted_;if(audio_)audio_->setMuted(muted_);}
                else if(!game_->usingTextInput()&&key=='I'){settings_.toggleInterpolation();saveSettings();}
            }
            // Latch racing input immediately; the presentation timer owns the
            // next repaint. Key repeats must not insert duplicate race frames.
            update(window,0);if(!inRace())InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_KEYUP:
            if(!game_){launcherView_.navigate(0,launcher_);return 0;}
            if(key<keys_.size())keys_[key]=false;
            if(key<blockedKeys_.size())blockedKeys_[key]=false;
            if(key=='F'&&!game_->usingTextInput())return 0;
            if(settings_.blocksGame()){navigateOverlay(window);return 0;}
            if(key=='P'&&!typingName())return 0;
            if(suppressInput_||key==VK_F10||key==VK_F11)return 0;
            if(const auto raw=nativeAmigaRawKey(unsigned(key),(unsigned(data)>>16)&255,(data&0x01000000)!=0))rawKeys_.push_back({*raw,false});
            update(window,0);return 0;
        case WM_SIZE:if(!game_)launcherView_.layout(window);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_WINDOWPOSCHANGED:{
            const auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
            if(monitor!=bufferMonitor_){bufferMonitor_=monitor;backBuffer_.reset();}
            return DefWindowProcW(window,kind,key,data);
        }
        case WM_DISPLAYCHANGE:backBuffer_.reset();display_.monitorChanged(window);if(game_)display_.apply(window,settings_.preferences(),true);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_PAINT:paint(window);return 0;
        case WM_CLOSE:closeApplication(window);return 0;
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
    try{
        const auto options=parse();PlayerDataOwnership ownership(options.paths.dataRoot);
        if(!ownership.acquired()){
            const auto window=FindWindowW(ownership.windowClassName().c_str(),nullptr);
            if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
            else MessageBoxW(nullptr,L"This game data folder is already in use. Close its other game window before trying again.",L"Stunt Car Racer",MB_OK|MB_ICONINFORMATION);
            return 0;
        }
        if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))SetProcessDPIAware();
        GameWindow game(options,ownership.windowClassName());return game.run(instance,show);
    }catch(const std::exception& error){MessageBoxW(nullptr,launcherErrorText(error).c_str(),L"Stunt Car Racer",MB_OK|MB_ICONERROR);return 1;}
}

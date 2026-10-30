#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "race/race_scene.h"
#include "platform/windows/drive_input.h"
#include "platform/windows/audio_output.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Scene=scr::runtime::RaceScene;
struct Options {
    std::filesystem::path chip,payload,assetPack,inputs,reference,trace,report,framesDirectory,audioWav,recordingsDirectory=L"analysis/live-driving";
    std::vector<unsigned> snapshots;std::vector<double> smoothSnapshots;
    unsigned startTick{},track{},startupSteps=256;bool headless{},paused{},interpolate{true},live{};
};
struct ExportedFrame {unsigned tick{};std::filesystem::path path;scr::RenderStatistics statistics{};};
struct ExportedSmoothFrame {double position{};std::filesystem::path path;};
struct InterpolationCheck {unsigned samples{},changedSamples{},exactEndpoints{},seekSamples{};double milliseconds{};};
std::uint64_t pixelHash(const std::vector<std::uint32_t>& pixels){
    std::uint64_t hash=14695981039346656037ULL;
    for(auto pixel:pixels){hash^=pixel;hash*=1099511628211ULL;}return hash;
}
struct RenderCoverage {
    unsigned nearFrames{},highlightFrames{};
    std::uint64_t queueEntries{},fillCommands{},highlightCommands{},highlightPixels{};
    void add(const scr::RenderStatistics& stats){
        nearFrames+=stats.nearQueueEntries>0;highlightFrames+=stats.edgeHighlightPixels>0;
        queueEntries+=stats.nearQueueEntries;fillCommands+=stats.nearFillCommands;
        highlightCommands+=stats.edgeHighlightCommands;highlightPixels+=stats.edgeHighlightPixels;
    }
};
std::string utf8(const std::wstring& value){
    if(value.empty())return {};
    const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(std::size_t(count),'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),result.data(),count,nullptr,nullptr);return result;
}
std::string jsonString(const std::string& value){
    std::string out="\"";for(char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(static_cast<unsigned char>(c)>=32)out+=c;}return out+'"';
}
Options parse(){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)throw std::runtime_error("Cannot parse command line");
    Options o;
    try{for(int n=1;n<count;++n){
        const std::wstring key=args[n];
        if(key==L"--headless"){o.headless=true;continue;}if(key==L"--paused"){o.paused=true;continue;}
        if(key==L"--no-interpolation"){o.interpolate=false;continue;}
        if(key==L"--live"){o.live=true;o.paused=true;continue;}
        if(n+1==count)throw std::runtime_error("Missing command-line value");const std::wstring value=args[++n];
        if(key==L"--chip")o.chip=value;else if(key==L"--payload")o.payload=value;else if(key==L"--inputs")o.inputs=value;
        else if(key==L"--reference")o.reference=value;else if(key==L"--trace")o.trace=value;else if(key==L"--report")o.report=value;
        else if(key==L"--frames-dir")o.framesDirectory=value;else if(key==L"--snapshot")o.snapshots.push_back(unsigned(std::stoul(value)));
        else if(key==L"--interpolated-snapshot")o.smoothSnapshots.push_back(std::stod(value));
        else if(key==L"--recordings-dir")o.recordingsDirectory=value;
        else if(key==L"--audio-wav")o.audioWav=value;
        else if(key==L"--asset-pack"){o.assetPack=value;o.live=true;o.paused=true;}
        else if(key==L"--track")o.track=unsigned(std::stoul(value));
        else if(key==L"--steps")o.startupSteps=unsigned(std::stoul(value));
        else if(key==L"--start-tick")o.startTick=unsigned(std::stoul(value));else throw std::runtime_error("Unknown command-line option: "+utf8(key));
    }}catch(...){LocalFree(args);throw;}LocalFree(args);
    if((o.assetPack.empty()&&(o.chip.empty()||o.payload.empty()||((!o.live||o.headless)&&(o.inputs.empty()||o.reference.empty()))))||o.trace.empty()||o.report.empty())
        throw std::runtime_error("Use scripts/start-native-replay.ps1 to prepare the verified replay assets and trace");
    return o;
}
void report(const Options& o,const Scene* scene,const std::vector<ExportedFrame>& images,unsigned renderedFrames=0,const std::string& error={},const RenderCoverage& coverage={},const InterpolationCheck& smooth={},const std::vector<ExportedSmoothFrame>& smoothImages={}) {
    if(o.report.empty())return;
    std::filesystem::create_directories(o.report.parent_path());std::ofstream out(o.report);
    if(!out)throw std::runtime_error("Cannot write viewer verification report");
    out<<"{\n  \"status\":\""<<(error.empty()?"passed":"failed")<<"\",\n  \"traceMatched\":"<<(error.empty()&&o.assetPack.empty()&&(!o.live||o.headless)?"true":"false")
       <<",\n  \"capture_input\":"<<(o.assetPack.empty()?"true":"false")
       <<",\n  \"mode\":\""<<(o.live?"live":"replay")<<"\",\n  \"cpu_emulation\":false,\n  \"ticks\":"<<(scene?(o.live?scene->liveTicks():scene->frames().size()):0)
       <<",\n  \"mesh_vertices\":"<<(scene?scene->mesh().vertices.size():0)<<",\n  \"mesh_faces\":"<<(scene?scene->mesh().faces.size():0)
       <<",\n  \"side_mesh_faces\":"<<(scene?scene->sideFaceCount():0)
       <<",\n  \"mountain_shapes\":"<<(scene?scene->mountainShapeCount():0)
       <<",\n  \"mountain_placements\":"<<(scene?scene->mountainPlacementCount():0)
       <<",\n  \"background_compressed_bytes\":"<<(scene?scene->backgroundCompressedBytes():0)
       <<",\n  \"rendered_frames\":"<<renderedFrames
       <<",\n  \"near_track_frames\":"<<coverage.nearFrames<<",\n  \"frames_with_edge_highlights\":"<<coverage.highlightFrames
       <<",\n  \"total_near_queue_entries\":"<<coverage.queueEntries<<",\n  \"total_near_fill_commands\":"<<coverage.fillCommands
       <<",\n  \"total_edge_highlight_commands\":"<<coverage.highlightCommands<<",\n  \"total_edge_highlight_pixels\":"<<coverage.highlightPixels
       <<",\n  \"snapshots\":[";
    for(unsigned n=0;n<images.size();++n){if(n)out<<',';const auto& e=images[n];out<<"\n    {\"tick\":"<<e.tick<<",\"path\":"<<jsonString(e.path.generic_string())
        <<",\"road_faces\":"<<e.statistics.roadFaces<<",\"side_faces\":"<<e.statistics.sideFaces
        <<",\"mountain_instances\":"<<e.statistics.mountainInstances<<",\"mountain_polygons\":"<<e.statistics.mountainPolygons
        <<",\"triangles\":"<<e.statistics.triangles<<",\"road_pixels\":"<<e.statistics.roadPixels
        <<",\"side_pixels\":"<<e.statistics.sidePixels<<",\"mountain_pixels\":"<<e.statistics.mountainPixels
        <<",\"near_queue_entries\":"<<e.statistics.nearQueueEntries<<",\"near_fill_commands\":"<<e.statistics.nearFillCommands
        <<",\"edge_highlight_commands\":"<<e.statistics.edgeHighlightCommands<<",\"edge_highlight_pixels\":"<<e.statistics.edgeHighlightPixels<<'}';}
    out<<"\n  ],\n  \"interpolation\":{\"target_fps\":"<<scr::runtime::presentationHz
       <<",\"physics_tick_seconds\":"<<scr::runtime::gameplayTickSeconds<<",\"rendered_samples\":"<<smooth.samples
       <<",\"changed_samples\":"<<smooth.changedSamples<<",\"exact_endpoints_checked\":"<<smooth.exactEndpoints
       <<",\"seek_samples_checked\":"<<smooth.seekSamples
       <<",\"elapsed_ms\":"<<smooth.milliseconds<<",\"snapshots\":[";
    for(unsigned n=0;n<smoothImages.size();++n){if(n)out<<',';const auto& image=smoothImages[n];
        out<<"{\"position\":"<<image.position<<",\"path\":"<<jsonString(image.path.generic_string())<<'}';}
    out<<"]}";if(o.live&&scene)out<<",\n  \"live_recording\":"<<jsonString(scene->liveRecording().generic_string());
    if(!error.empty())out<<",\n  \"error\":"<<jsonString(error);out<<"\n}\n";
}
HFONT makeFont(int size,int weight=FW_NORMAL){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
COLORREF rgb(unsigned r,unsigned g,unsigned b){return RGB(r,g,b);}
void exportAudio(const Scene& scene,const std::filesystem::path& path){
    const auto frames=std::uint64_t(scene.frames().size())*5760;
    if(frames>0x3fffffff-44)throw std::runtime_error("Audio export exceeds WAV size limit");
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Cannot create audio export");
    const auto word=[&](std::uint16_t v){out.put(char(v));out.put(char(v>>8));};
    const auto longword=[&](std::uint32_t v){word(std::uint16_t(v));word(std::uint16_t(v>>16));};
    out.write("RIFF",4);longword(std::uint32_t(frames*4+36));out.write("WAVEfmt ",8);longword(16);
    word(1);word(2);longword(48000);longword(192000);word(4);word(16);out.write("data",4);longword(std::uint32_t(frames*4));
    scr::runtime::GameAudio audio(scene.audioSeed());
    for(const auto& frame:scene.frames()){
        audio.submit(frame.audio);const auto pcm=audio.renderFrames(5760);for(auto sample:pcm)word(std::uint16_t(sample));
    }
    if(!out)throw std::runtime_error("Audio export write failed");
    const auto& stats=audio.statistics();std::ofstream report(path.string()+".json");
    report<<"{\"sample_rate\":48000,\"channels\":2,\"frames\":"<<stats.frames<<",\"vblanks\":"<<stats.vblanks
          <<",\"interrupts\":"<<stats.interrupts<<",\"effect_starts\":"<<stats.effectStarts
          <<",\"minimum_period\":"<<stats.minimumPeriod<<",\"maximum_period\":"<<stats.maximumPeriod<<",\"cpu_emulation\":false}\n";
}
void fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
void text(HDC dc,RECT r,const std::wstring& value,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER){
    auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),int(value.size()),&r,flags);SelectObject(dc,previous);
}
class Window {
public:
    Window(Scene& scene,const Options& options):scene_(scene),position_(std::min(options.startTick,unsigned(scene.frames().size()-1))),paused_(options.paused),interpolate_(options.interpolate),live_(options.live){
        titleFont_=makeFont(22,FW_SEMIBOLD);bodyFont_=makeFont(15);smallFont_=makeFont(13);buttonFont_=makeFont(15,FW_SEMIBOLD);
        if(live_)audio_=std::make_unique<scr::runtime::WindowsAudioOutput>(scene_.audioSeed());
    }
    ~Window(){if(timer_)CloseHandle(timer_);DeleteObject(titleFont_);DeleteObject(bodyFont_);DeleteObject(smallFont_);DeleteObject(buttonFont_);}
    int run(HINSTANCE instance,int show){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=procedure;wc.lpszClassName=L"StuntCarRacerNativeReplay";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
        RECT rect{0,0,1024,800};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        auto window=CreateWindowExW(0,wc.lpszClassName,live_?L"Stunt Car Racer — Native driving":L"Stunt Car Racer — Native replay",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("Cannot create the native replay window");
        // A one-shot high-resolution timer retains fractional60Hz deadlines;
        // WM_TIMER's millisecond/coalesced cadence was about39FPS on this host.
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)throw std::runtime_error("Cannot create replay presentation timer");
        last_=fpsStart_=std::chrono::steady_clock::now();auto deadline=last_;
        const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0/scr::runtime::presentationHz));
        const auto arm=[&](){
            const auto remaining=std::chrono::duration<double>(deadline-std::chrono::steady_clock::now()).count();
            LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,LONGLONG(std::ceil(remaining*10000000)));
            if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))throw std::runtime_error("Cannot arm replay presentation timer");
        };
        ShowWindow(window,show);UpdateWindow(window);arm();MSG msg{};
        for(;;){
            const auto wake=MsgWaitForMultipleObjectsEx(1,&timer_,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(wake==WAIT_FAILED)throw std::runtime_error("Replay presentation wait failed");
            if(wake==WAIT_OBJECT_0){
                const auto now=std::chrono::steady_clock::now();const double delta=std::min(0.25,std::chrono::duration<double>(now-last_).count());last_=now;
                if(live_)updateLive(window,delta);
                else if(!paused_&&!dragging_){
                    position_+=delta*speeds_[speedIndex_]/scr::runtime::gameplayTickSeconds;
                    if(position_>=scene_.frames().size()-1){position_=double(scene_.frames().size()-1);paused_=true;}
                    if(!IsIconic(window)){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);}
                }
                deadline+=period;
                const auto finished=std::chrono::steady_clock::now();
                if(deadline<=finished)deadline+=period*((finished-deadline)/period+1);
                arm();
            }
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                if(msg.message==WM_QUIT)return int(msg.wParam);
                TranslateMessage(&msg);DispatchMessageW(&msg);
            }
        }
    }
private:
    Scene& scene_;double position_{};bool paused_{},dragging_{},wireframe_{},interpolate_{true},live_{};unsigned speedIndex_=1;
    scr::runtime::WindowsDriveInput driveInput_;
    scr::runtime::PolledDrivePad drivePad_;
    scr::runtime::DriveInputLatch driveLatch_;
    scr::runtime::FixedDriveClock driveClock_;
    std::unique_ptr<scr::runtime::WindowsAudioOutput> audio_;
    bool muted_{};
    std::array<bool,256> keys_{};
    HANDLE timer_{};
    std::chrono::steady_clock::time_point last_=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point fpsStart_=std::chrono::steady_clock::now();
    unsigned presentedFrames_{};double framesPerSecond_{};
    HFONT titleFont_{},bodyFont_{},smallFont_{},buttonFont_{};RECT image_{},timeline_{};std::array<RECT,4> buttons_{};
    static constexpr double speeds_[4]{0.5,1,2,4};
    unsigned index() const{return live_?1:std::min(unsigned(position_),unsigned(scene_.frames().size()-1));}
    unsigned driveMask()const{
        return scr::runtime::mapDriveControls({keys_['W']||keys_[VK_UP],keys_['S']||keys_[VK_DOWN],keys_['A']||keys_[VK_LEFT],keys_['D']||keys_[VK_RIGHT],keys_[VK_SPACE]},drivePad_.pad);
    }
    void resetLive(){scene_.resetLive();driveClock_.reset();driveLatch_.clear();keys_.fill(false);paused_=false;resetFrameCounter();audio_->reset(scene_.audioSeed());audio_->setPlaying(true);}
    void updateLive(HWND window,double elapsed){
        const bool connected=drivePad_.pad.connected;drivePad_=driveInput_.poll();const bool focused=GetForegroundWindow()==window;
        if(drivePad_.disconnected){paused_=true;driveLatch_.clear();keys_.fill(false);}
        if(focused&&drivePad_.resetPressed)resetLive();else if(focused&&drivePad_.pausePressed)toggle();
        if(!paused_&&focused){
            driveLatch_.sample(driveMask());const auto steps=driveClock_.advance(elapsed);
            for(unsigned n=0;n<steps;++n){
                if(!scene_.stepLive(driveLatch_.consume())){paused_=true;break;}
                audio_->submit(scene_.frames().back().audio);
                if(scene_.liveTerminal()){paused_=true;break;}
            }
            InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
        }else if(connected!=drivePad_.pad.connected||drivePad_.pausePressed||drivePad_.resetPressed){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);}
        audio_->setPlaying(!paused_&&focused);
    }
    void layout(int width,int height){
        const int availableHeight=std::max(200,height-190);
        const auto layout=scr::runtime::fitPresentationImage(320,200,std::max(1,width-48),availableHeight);
        const int w=layout.width,h=layout.height;const int x=24+layout.x,y=76+layout.y;
        image_={x,y,x+w,y+h};const int bottom=std::max(y+h+20,height-98);
        timeline_={32,bottom+8,width-32,bottom+24};
        for(unsigned n=0;n<buttons_.size();++n)buttons_[n]={32+int(n)*108,bottom+38,128+int(n)*108,bottom+72};
    }
    std::wstring time(const scr::runtime::RaceFrame& frame)const{
        std::wostringstream s;s<<std::hex<<std::setfill(L'0')<<std::setw(2)<<unsigned(frame.minutes)<<L":"<<std::setw(2)<<unsigned(frame.seconds)<<L"."<<std::setw(2)<<unsigned(frame.subseconds);return s.str();
    }
    void paint(HWND window){
        PAINTSTRUCT ps{};auto target=BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);layout(client.right,client.bottom);
        auto dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,std::max(1L,client.right),std::max(1L,client.bottom));auto oldBitmap=SelectObject(dc,bitmap);
        fill(dc,client,rgb(16,20,25));fill(dc,{0,0,client.right,64},rgb(23,29,36));fill(dc,{0,63,client.right,65},rgb(48,56,65));
        text(dc,{30,9,client.right-30,39},L"STUNT CAR RACER",titleFont_,rgb(244,232,213));
        const auto subtitle=live_?(interpolate_?L"Live driving  ·  60 FPS  ·  Physics 8.33 Hz":L"Live driving  ·  Exact ticks  ·  Physics 8.33 Hz"):(interpolate_?L"60 FPS interpolation  ·  Verified physics 8.33 Hz":L"Exact ticks  ·  Verified physics 8.33 Hz");
        text(dc,{31,37,client.right-230,58},subtitle,smallFont_,rgb(158,175,186));
        const auto& frame=scene_.frames()[index()];std::wostringstream right;
        right<<L"Lap "<<std::max(1u,std::min(3u,unsigned(frame.lapCounter)))<<L" / 3     "<<time(frame);
        text(dc,{client.right-320,10,client.right-30,43},right.str(),bodyFont_,rgb(214,224,229),DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        std::wostringstream performance;
        if(paused_)performance<<L"— FPS · paused";
        else if(framesPerSecond_==0)performance<<L"— FPS";
        else performance<<std::fixed<<std::setprecision(1)<<framesPerSecond_<<L" FPS";
        text(dc,{client.right-220,37,client.right-30,58},performance.str(),smallFont_,rgb(158,175,186),DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        const auto& pixels=live_?scene_.renderLive(driveClock_.fraction(),wireframe_,interpolate_):(interpolate_?scene_.renderInterpolated(position_,wireframe_):scene_.render(index(),wireframe_));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=320;info.bmiHeader.biHeight=-200;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,image_.left,image_.top,image_.right-image_.left,image_.bottom-image_.top,0,0,320,200,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        if(frame.action==2){
            RECT overlay{image_.left+image_.right/2-image_.left/2-140,image_.top+26,image_.left+image_.right/2-image_.left/2+140,image_.top+72};
            fill(dc,overlay,rgb(22,27,32));text(dc,overlay,L"RACE COMPLETE",buttonFont_,rgb(246,220,151),DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        }
        if(live_)text(dc,{32,timeline_.top-5,client.right-32,timeline_.bottom+8},drivePad_.pad.connected?L"Xbox controller connected  ·  Stick / D-pad steer  ·  RT gas  ·  LT brake  ·  A boost":L"Keyboard ready  ·  Arrows / WASD drive  ·  SPACE boost  ·  Xbox controller: disconnected",smallFont_,rgb(182,199,205));
        else{
            fill(dc,timeline_,rgb(47,57,68));RECT progress=timeline_;progress.right=progress.left+int((progress.right-progress.left)*position_/double(scene_.frames().size()-1));fill(dc,progress,rgb(187,84,61));
            const int marker=progress.right;fill(dc,{marker-2,timeline_.top-3,marker+3,timeline_.bottom+3},rgb(247,225,196));
        }
        const std::array<std::wstring,4> labels{paused_?(live_?L"Drive":L"Play"):L"Pause",live_?L"Reset":L"Restart",live_?(interpolate_?L"Smooth":L"Exact ticks"):L"Step",std::array<std::wstring,4>{L"0.5×",L"1×",L"2×",L"4×"}[speedIndex_]};
        for(unsigned n=0;n<(live_?3u:4u);++n){fill(dc,buttons_[n],n==0?rgb(150,62,46):rgb(38,47,57));text(dc,buttons_[n],labels[n],buttonFont_,rgb(237,234,226),DT_CENTER|DT_SINGLELINE|DT_VCENTER);}
        std::wostringstream details;if(live_){
            const auto error=audio_->error();
            details<<L"Update "<<scene_.liveTicks()<<L"   ·   "<<(error.empty()?(muted_?L"Muted":L"Sound on"):L"Audio unavailable");
        }else details<<L"Update "<<index()+1<<L" / "<<scene_.frames().size()<<L"   ·   "<<frame.restarts<<L" restarts";
        text(dc,{480,buttons_[0].top,client.right-32,buttons_[0].bottom},details.str(),smallFont_,rgb(157,175,188),DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        text(dc,{32,client.bottom-24,client.right-32,client.bottom-5},live_?L"ARROWS / WASD drive    SPACE boost    P pause    R reset    M mute    I smooth    F2 wireframe":L"SPACE play / pause     ← → step     HOME restart     I interpolation     W wireframe",smallFont_,rgb(120,140,153));
        BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&ps);
        if(!paused_){
            ++presentedFrames_;const auto now=std::chrono::steady_clock::now();
            const auto elapsed=std::chrono::duration<double>(now-fpsStart_).count();
            if(elapsed>=1){framesPerSecond_=presentedFrames_/elapsed;presentedFrames_=0;fpsStart_=now;}
        }
    }
    void seek(int x){const double t=std::clamp(double(x-timeline_.left)/std::max(1L,timeline_.right-timeline_.left),0.0,1.0);position_=t*double(scene_.frames().size()-1);}
    void resetFrameCounter(){presentedFrames_=0;framesPerSecond_=0;fpsStart_=std::chrono::steady_clock::now();}
    void toggle(){
        if(live_){if(scene_.liveTerminal())return;driveLatch_.clear();keys_.fill(false);}
        else if(position_>=scene_.frames().size()-1)position_=0;
        paused_=!paused_;if(!paused_){last_=std::chrono::steady_clock::now();resetFrameCounter();}
        if(audio_)audio_->setPlaying(!paused_);
    }
    LRESULT message(HWND window,UINT message,WPARAM w,LPARAM l){
        switch(message){
        case WM_ERASEBKGND:return 1;
        case WM_GETMINMAXINFO:{auto* p=reinterpret_cast<MINMAXINFO*>(l);p->ptMinTrackSize={690,650};return 0;}
        case WM_SIZE:InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_ACTIVATEAPP:if(live_&&!w){paused_=true;keys_.fill(false);driveLatch_.clear();audio_->setPlaying(false);InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_KEYUP:if(live_&&w<keys_.size()){keys_[w]=false;if(!paused_)driveLatch_.sample(driveMask());}return 0;
        case WM_KEYDOWN:
            if(live_){
                if(w<keys_.size())keys_[w]=true;
                if(!(l&0x40000000)){
                    if(w=='P'||w==VK_ESCAPE)toggle();else if(w=='R')resetLive();else if(w=='I')interpolate_=!interpolate_;else if(w==VK_F2)wireframe_=!wireframe_;
                    else if(w=='M'){muted_=!muted_;audio_->setMuted(muted_);}
                }
                if(!paused_)driveLatch_.sample(driveMask());InvalidateRect(window,nullptr,FALSE);return 0;
            }
            if(w==VK_SPACE)toggle();else if(w==VK_RIGHT){paused_=true;position_=std::min(double(scene_.frames().size()-1),std::floor(position_)+1);}
            else if(w==VK_LEFT){paused_=true;position_=std::max(0.0,std::floor(position_)-1);}else if(w==VK_HOME)position_=0;
            else if(w==VK_END){paused_=true;position_=double(scene_.frames().size()-1);}else if(w=='W')wireframe_=!wireframe_;
            else if(w=='I')interpolate_=!interpolate_;
            InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:{const POINT p{int(short(LOWORD(l))),int(short(HIWORD(l)))};
            if(live_){
                for(unsigned n=0;n<3;++n)if(PtInRect(&buttons_[n],p)){if(!n)toggle();else if(n==1)resetLive();else interpolate_=!interpolate_;}
                InvalidateRect(window,nullptr,FALSE);return 0;
            }
            if(PtInRect(&timeline_,p)){dragging_=true;SetCapture(window);seek(p.x);}
            else for(unsigned n=0;n<buttons_.size();++n)if(PtInRect(&buttons_[n],p)){
                if(n==0)toggle();else if(n==1){position_=0;paused_=false;resetFrameCounter();}else if(n==2){paused_=true;position_=std::min(double(scene_.frames().size()-1),std::floor(position_)+1);}else speedIndex_=(speedIndex_+1)%4;
            }InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_MOUSEMOVE:if(dragging_){seek(int(short(LOWORD(l))));InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_LBUTTONUP:if(dragging_){dragging_=false;ReleaseCapture();}return 0;
        case WM_PAINT:paint(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        default:return DefWindowProcW(window,message,w,l);
        }
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
        Window* self=reinterpret_cast<Window*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,w,l);
        try{return self->message(window,message,w,l);}catch(const std::exception& e){MessageBoxA(window,e.what(),"Native replay rendering failed",MB_ICONERROR|MB_OK);DestroyWindow(window);return 0;}
    }
};
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    Options options;std::unique_ptr<Scene> scene;
    try{
        options=parse();std::filesystem::create_directories(options.trace.parent_path());
        scene=std::make_unique<Scene>();
        if(options.live){
            if(options.assetPack.empty())scene->loadLive(options.chip,options.payload,options.recordingsDirectory);
            else scene->loadNative(options.assetPack,options.recordingsDirectory,options.track);
            unsigned presentations=0;
            if(options.headless){
                if(options.assetPack.empty())presentations=scene->verifyLiveReplay(options.inputs,options.reference,options.trace);
                else for(unsigned tick=0;tick<options.startupSteps;++tick){
                    if(!scene->stepLive(tick<options.startupSteps*3/4?1:2))break;
                    scene->renderLive(0.5);++presentations;if(scene->liveTerminal())break;
                }
            }
            report(options,scene.get(),{},presentations);
            if(options.headless)return 0;
            SetProcessDPIAware();Window window(*scene,options);const auto result=window.run(instance,show);
            report(options,scene.get(),{});return result;
        }
        scene->load(options.chip,options.payload,options.inputs,options.reference,options.trace);
        if(!options.audioWav.empty())exportAudio(*scene,options.audioWav);
        // Exercise every camera and suspension state before accepting a headless
        // check. Snapshot export alone would leave most seek positions untested.
        unsigned renderedFrames=0;RenderCoverage coverage;InterpolationCheck smooth;
        if(options.headless){
            std::vector<std::uint64_t> exactHashes;
            for(unsigned tick=0;tick<scene->frames().size();++tick){exactHashes.push_back(pixelHash(scene->render(tick)));coverage.add(scene->statistics());++renderedFrames;}
            const auto started=std::chrono::steady_clock::now();
            const auto last=double(scene->frames().size()-1);
            const auto intervals=unsigned(std::ceil(last*scr::runtime::gameplayTickSeconds*scr::runtime::presentationHz-1e-9));
            std::vector<std::pair<double,std::uint64_t>> seekChecks;
            for(unsigned sample=0;sample<=intervals;++sample){
                const double position=std::min(last,double(sample)/(scr::runtime::gameplayTickSeconds*scr::runtime::presentationHz));
                const auto hash=pixelHash(scene->renderInterpolated(position));++smooth.samples;
                if(sample%37==0)seekChecks.push_back({position,hash});
                if(hash!=exactHashes[unsigned(position)]&&hash!=exactHashes[std::min(unsigned(position)+1,unsigned(last))])++smooth.changedSamples;
            }
            for(auto sample=seekChecks.rbegin();sample!=seekChecks.rend();++sample){
                if(pixelHash(scene->renderInterpolated(sample->first))!=sample->second)throw std::runtime_error("Interpolated replay depends on seek order");
                ++smooth.seekSamples;
            }
            for(unsigned tick=0;tick<scene->frames().size();++tick){
                if(pixelHash(scene->renderInterpolated(tick))!=exactHashes[tick])throw std::runtime_error("Interpolated rendering changed an exact verified frame");
                ++smooth.exactEndpoints;
            }
            smooth.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        }
        std::vector<ExportedFrame> images;
        if(!options.snapshots.empty()){
            if(options.framesDirectory.empty())throw std::runtime_error("Frame export requires --frames-dir");
            std::filesystem::create_directories(options.framesDirectory);
            for(auto tick:options.snapshots){if(tick>=scene->frames().size())throw std::runtime_error("Requested snapshot exceeds replay");scene->render(tick);
                std::ostringstream name;name<<"frame-"<<std::setw(4)<<std::setfill('0')<<tick<<".bmp";const auto path=options.framesDirectory/name.str();scene->saveBitmap(path);images.push_back({tick,path,scene->statistics()});}
        }
        std::vector<ExportedSmoothFrame> smoothImages;
        if(!options.smoothSnapshots.empty()){
            if(options.framesDirectory.empty())throw std::runtime_error("Interpolated export requires --frames-dir");
            std::filesystem::create_directories(options.framesDirectory);
            for(auto position:options.smoothSnapshots){
                if(!std::isfinite(position)||position<0||position>scene->frames().size()-1)throw std::runtime_error("Interpolated snapshot exceeds replay");
                scene->renderInterpolated(position);std::ostringstream name;name<<"interpolated-"<<std::fixed<<std::setprecision(3)<<position<<".bmp";
                const auto path=options.framesDirectory/name.str();scene->saveBitmap(path);smoothImages.push_back({position,path});
            }
        }
        report(options,scene.get(),images,renderedFrames,{},coverage,smooth,smoothImages);if(options.headless)return 0;
        SetProcessDPIAware();Window window(*scene,options);return window.run(instance,show);
    }catch(const std::exception& e){
        try{report(options,scene.get(),{},0,e.what());}catch(...){}
        if(!options.headless)MessageBoxA(nullptr,e.what(),"Cannot start native replay",MB_ICONERROR|MB_OK);return 1;
    }
}

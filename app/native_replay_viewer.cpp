#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "replay_scene.h"
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
using Scene=scr::viewer::ReplayScene;
struct Options {
    std::filesystem::path chip,payload,inputs,reference,trace,report,framesDirectory;
    std::vector<unsigned> snapshots;unsigned startTick{};bool headless{},paused{};
};
struct ExportedFrame {unsigned tick{};std::filesystem::path path;scr::RenderStatistics statistics{};};
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
        if(n+1==count)throw std::runtime_error("Missing command-line value");const std::wstring value=args[++n];
        if(key==L"--chip")o.chip=value;else if(key==L"--payload")o.payload=value;else if(key==L"--inputs")o.inputs=value;
        else if(key==L"--reference")o.reference=value;else if(key==L"--trace")o.trace=value;else if(key==L"--report")o.report=value;
        else if(key==L"--frames-dir")o.framesDirectory=value;else if(key==L"--snapshot")o.snapshots.push_back(unsigned(std::stoul(value)));
        else if(key==L"--start-tick")o.startTick=unsigned(std::stoul(value));else throw std::runtime_error("Unknown command-line option: "+utf8(key));
    }}catch(...){LocalFree(args);throw;}LocalFree(args);
    if(o.chip.empty()||o.payload.empty()||o.inputs.empty()||o.reference.empty()||o.trace.empty()||o.report.empty())
        throw std::runtime_error("Use scripts/start-native-replay.ps1 to prepare the verified replay assets and trace");
    return o;
}
void report(const Options& o,const Scene* scene,const std::vector<ExportedFrame>& images,unsigned renderedFrames=0,const std::string& error={}) {
    if(o.report.empty())return;
    std::filesystem::create_directories(o.report.parent_path());std::ofstream out(o.report);
    if(!out)throw std::runtime_error("Cannot write viewer verification report");
    out<<"{\n  \"status\":\""<<(error.empty()?"passed":"failed")<<"\",\n  \"traceMatched\":"<<(error.empty()?"true":"false")
       <<",\n  \"cpu_emulation\":false,\n  \"ticks\":"<<(scene?scene->frames().size():0)
       <<",\n  \"mesh_vertices\":"<<(scene?scene->mesh().vertices.size():0)<<",\n  \"mesh_faces\":"<<(scene?scene->mesh().faces.size():0)
       <<",\n  \"side_mesh_faces\":"<<(scene?scene->sideFaceCount():0)
       <<",\n  \"mountain_shapes\":"<<(scene?scene->mountainShapeCount():0)
       <<",\n  \"mountain_placements\":"<<(scene?scene->mountainPlacementCount():0)
       <<",\n  \"background_compressed_bytes\":"<<(scene?scene->backgroundCompressedBytes():0)
       <<",\n  \"rendered_frames\":"<<renderedFrames<<",\n  \"snapshots\":[";
    for(unsigned n=0;n<images.size();++n){if(n)out<<',';const auto& e=images[n];out<<"\n    {\"tick\":"<<e.tick<<",\"path\":"<<jsonString(e.path.generic_string())
        <<",\"road_faces\":"<<e.statistics.roadFaces<<",\"side_faces\":"<<e.statistics.sideFaces
        <<",\"mountain_instances\":"<<e.statistics.mountainInstances<<",\"mountain_polygons\":"<<e.statistics.mountainPolygons
        <<",\"triangles\":"<<e.statistics.triangles<<",\"road_pixels\":"<<e.statistics.roadPixels
        <<",\"side_pixels\":"<<e.statistics.sidePixels<<",\"mountain_pixels\":"<<e.statistics.mountainPixels<<'}';}
    out<<"\n  ]";if(!error.empty())out<<",\n  \"error\":"<<jsonString(error);out<<"\n}\n";
}
HFONT makeFont(int size,int weight=FW_NORMAL){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
COLORREF rgb(unsigned r,unsigned g,unsigned b){return RGB(r,g,b);}
void fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
void text(HDC dc,RECT r,const std::wstring& value,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER){
    auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),int(value.size()),&r,flags);SelectObject(dc,previous);
}
class Window {
public:
    Window(Scene& scene,const Options& options):scene_(scene),position_(std::min(options.startTick,unsigned(scene.frames().size()-1))),paused_(options.paused){
        titleFont_=makeFont(22,FW_SEMIBOLD);bodyFont_=makeFont(15);smallFont_=makeFont(13);buttonFont_=makeFont(15,FW_SEMIBOLD);
    }
    ~Window(){DeleteObject(titleFont_);DeleteObject(bodyFont_);DeleteObject(smallFont_);DeleteObject(buttonFont_);}
    int run(HINSTANCE instance,int show){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=procedure;wc.lpszClassName=L"StuntCarRacerNativeReplay";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
        RECT rect{0,0,1024,800};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        auto window=CreateWindowExW(0,wc.lpszClassName,L"Stunt Car Racer — Native replay",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("Cannot create the native replay window");
        ShowWindow(window,show);UpdateWindow(window);MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return int(msg.wParam);
    }
private:
    Scene& scene_;double position_{};bool paused_{},dragging_{},wireframe_{};unsigned speedIndex_=1;
    std::chrono::steady_clock::time_point last_=std::chrono::steady_clock::now();
    HFONT titleFont_{},bodyFont_{},smallFont_{},buttonFont_{};RECT image_{},timeline_{};std::array<RECT,4> buttons_{};
    static constexpr double speeds_[4]{0.5,1,2,4};
    unsigned index() const{return std::min(unsigned(position_),unsigned(scene_.frames().size()-1));}
    void layout(int width,int height){
        const int availableHeight=std::max(200,height-190);const int scale=std::max(1,std::min((width-48)/320,availableHeight/200));
        const int w=320*scale,h=200*scale;const int x=(width-w)/2,y=76+std::max(0,(availableHeight-h)/2);
        image_={x,y,x+w,y+h};const int bottom=std::max(y+h+20,height-98);
        timeline_={32,bottom+8,width-32,bottom+24};
        for(unsigned n=0;n<buttons_.size();++n)buttons_[n]={32+int(n)*108,bottom+38,128+int(n)*108,bottom+72};
    }
    std::wstring time(const scr::viewer::ReplaySceneFrame& frame)const{
        std::wostringstream s;s<<std::hex<<std::setfill(L'0')<<std::setw(2)<<unsigned(frame.minutes)<<L":"<<std::setw(2)<<unsigned(frame.seconds)<<L"."<<std::setw(2)<<unsigned(frame.subseconds);return s.str();
    }
    void paint(HWND window){
        PAINTSTRUCT ps{};auto target=BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);layout(client.right,client.bottom);
        auto dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,std::max(1L,client.right),std::max(1L,client.bottom));auto oldBitmap=SelectObject(dc,bitmap);
        fill(dc,client,rgb(16,20,25));fill(dc,{0,0,client.right,64},rgb(23,29,36));fill(dc,{0,63,client.right,65},rgb(48,56,65));
        text(dc,{30,9,client.right-30,39},L"STUNT CAR RACER",titleFont_,rgb(244,232,213));
        text(dc,{31,37,client.right-30,58},L"Native replay  ·  Original trace checked",smallFont_,rgb(158,175,186));
        const auto& frame=scene_.frames()[index()];std::wostringstream right;
        right<<L"Lap "<<std::max(1u,std::min(3u,unsigned(frame.lapCounter)))<<L" / 3     "<<time(frame);
        text(dc,{client.right-320,10,client.right-30,43},right.str(),bodyFont_,rgb(214,224,229),DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        const auto& pixels=scene_.render(index(),wireframe_);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=320;info.bmiHeader.biHeight=-200;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,image_.left,image_.top,image_.right-image_.left,image_.bottom-image_.top,0,0,320,200,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        if(frame.action==2){
            RECT overlay{image_.left+image_.right/2-image_.left/2-140,image_.top+26,image_.left+image_.right/2-image_.left/2+140,image_.top+72};
            fill(dc,overlay,rgb(22,27,32));text(dc,overlay,L"RACE COMPLETE",buttonFont_,rgb(246,220,151),DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        }
        fill(dc,timeline_,rgb(47,57,68));RECT progress=timeline_;progress.right=progress.left+int((progress.right-progress.left)*position_/double(scene_.frames().size()-1));fill(dc,progress,rgb(187,84,61));
        const int marker=progress.right;fill(dc,{marker-2,timeline_.top-3,marker+3,timeline_.bottom+3},rgb(247,225,196));
        const std::array<std::wstring,4> labels{paused_?L"Play":L"Pause",L"Restart",L"Step",std::array<std::wstring,4>{L"0.5×",L"1×",L"2×",L"4×"}[speedIndex_]};
        for(unsigned n=0;n<buttons_.size();++n){fill(dc,buttons_[n],n==0?rgb(150,62,46):rgb(38,47,57));text(dc,buttons_[n],labels[n],buttonFont_,rgb(237,234,226),DT_CENTER|DT_SINGLELINE|DT_VCENTER);}
        std::wostringstream details;details<<L"Update "<<index()+1<<L" / "<<scene_.frames().size()<<L"   ·   "<<frame.restarts<<L" restarts";
        text(dc,{480,buttons_[0].top,client.right-32,buttons_[0].bottom},details.str(),smallFont_,rgb(157,175,188),DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        text(dc,{32,client.bottom-24,client.right-32,client.bottom-5},L"SPACE play / pause     ← → step     HOME restart     Click the timeline to seek",smallFont_,rgb(120,140,153));
        BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&ps);
    }
    void seek(int x){const double t=std::clamp(double(x-timeline_.left)/std::max(1L,timeline_.right-timeline_.left),0.0,1.0);position_=t*double(scene_.frames().size()-1);}
    void toggle(){if(position_>=scene_.frames().size()-1)position_=0;paused_=!paused_;}
    LRESULT message(HWND window,UINT message,WPARAM w,LPARAM l){
        switch(message){
        case WM_CREATE:SetTimer(window,1,16,nullptr);return 0;
        case WM_ERASEBKGND:return 1;
        case WM_GETMINMAXINFO:{auto* p=reinterpret_cast<MINMAXINFO*>(l);p->ptMinTrackSize={690,650};return 0;}
        case WM_SIZE:InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_TIMER:{const auto now=std::chrono::steady_clock::now();const double delta=std::min(0.25,std::chrono::duration<double>(now-last_).count());last_=now;
            if(!paused_&&!dragging_){position_+=delta*speeds_[speedIndex_]/0.12;if(position_>=scene_.frames().size()-1){position_=double(scene_.frames().size()-1);paused_=true;}InvalidateRect(window,nullptr,FALSE);}return 0;}
        case WM_KEYDOWN:
            if(w==VK_SPACE)toggle();else if(w==VK_RIGHT){paused_=true;position_=std::min(double(scene_.frames().size()-1),std::floor(position_)+1);}
            else if(w==VK_LEFT){paused_=true;position_=std::max(0.0,std::floor(position_)-1);}else if(w==VK_HOME)position_=0;
            else if(w==VK_END){paused_=true;position_=double(scene_.frames().size()-1);}else if(w=='W')wireframe_=!wireframe_;
            InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:{const POINT p{int(short(LOWORD(l))),int(short(HIWORD(l)))};
            if(PtInRect(&timeline_,p)){dragging_=true;SetCapture(window);seek(p.x);}
            else for(unsigned n=0;n<buttons_.size();++n)if(PtInRect(&buttons_[n],p)){
                if(n==0)toggle();else if(n==1){position_=0;paused_=false;}else if(n==2){paused_=true;position_=std::min(double(scene_.frames().size()-1),std::floor(position_)+1);}else speedIndex_=(speedIndex_+1)%4;
            }InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_MOUSEMOVE:if(dragging_){seek(int(short(LOWORD(l))));InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_LBUTTONUP:if(dragging_){dragging_=false;ReleaseCapture();}return 0;
        case WM_PAINT:paint(window);return 0;
        case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
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
        scene=std::make_unique<Scene>();scene->load(options.chip,options.payload,options.inputs,options.reference,options.trace);
        // Exercise every camera and suspension state before accepting a headless
        // check. Snapshot export alone would leave most seek positions untested.
        unsigned renderedFrames=0;
        if(options.headless)for(unsigned tick=0;tick<scene->frames().size();++tick){scene->render(tick);++renderedFrames;}
        std::vector<ExportedFrame> images;
        if(!options.snapshots.empty()){
            if(options.framesDirectory.empty())throw std::runtime_error("Frame export requires --frames-dir");
            std::filesystem::create_directories(options.framesDirectory);
            for(auto tick:options.snapshots){if(tick>=scene->frames().size())throw std::runtime_error("Requested snapshot exceeds replay");scene->render(tick);
                std::ostringstream name;name<<"frame-"<<std::setw(4)<<std::setfill('0')<<tick<<".bmp";const auto path=options.framesDirectory/name.str();scene->saveBitmap(path);images.push_back({tick,path,scene->statistics()});}
        }
        report(options,scene.get(),images,renderedFrames);if(options.headless)return 0;
        SetProcessDPIAware();Window window(*scene,options);return window.run(instance,show);
    }catch(const std::exception& e){
        try{report(options,scene.get(),{},0,e.what());}catch(...){}
        if(!options.headless)MessageBoxA(nullptr,e.what(),"Cannot start native replay",MB_ICONERROR|MB_OK);return 1;
    }
}

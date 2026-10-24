#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "native_launcher.h"
#include "windows_launcher.h"
#include "local_asset_pack.h"
#include "replay_scene.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void write(const std::filesystem::path& path,const std::string& value){std::ofstream output(path,std::ios::binary);output.write(value.data(),std::streamsize(value.size()));}
NativeLaunchPaths paths(const std::filesystem::path& root){return {root/L"game.scr-assets",root/L"display.ini",root/L"game.scr-save",{}};}
void assets(const std::filesystem::path& pack,const std::filesystem::path& root,const std::filesystem::path& adf){
    auto files=paths(root);NativeLauncher missing(paths(root/L"never-imported"));require(!missing.ready(),"Missing data enabled Play");
    std::filesystem::copy_file(pack,files.pack,std::filesystem::copy_options::overwrite_existing);
    NativeLauncher ready(files);require(ready.ready(),"Verified pack did not enable Play");
    const auto before=bytes(files.pack);const auto unsupported=root/L"unsupported.adf";write(unsupported,std::string(901120,'x'));
    require(!ready.importDisk(unsupported)&&ready.ready()&&bytes(files.pack)==before,"Unsupported replacement destroyed good data");
    require(!ready.importDisk(root/L"absent.adf")&&ready.ready(),"Unreadable replacement disabled good data");
    write(files.pack,"damaged");ready.refreshAssets();require(!ready.ready()&&!ready.error().empty(),"Corrupt data did not enter recovery");
    std::filesystem::copy_file(pack,files.pack,std::filesystem::copy_options::overwrite_existing);
    if(!adf.empty()){
        const auto original=bytes(adf);importLocalAssetPack(adf,files.pack);
        require(bytes(files.pack)==before&&bytes(adf)==original,"Native import differs from Python pack or changed the ADF");
        require(localAssetSourceName(files.pack)==adf.filename().wstring(),"Imported source filename was lost");
        const auto unicodeSource=root/L"source-\u00e9-\u8eca.adf";std::filesystem::copy_file(adf,unicodeSource,std::filesystem::copy_options::overwrite_existing);
        importLocalAssetPack(unicodeSource,files.pack);require(localAssetSourceName(files.pack)==unicodeSource.filename().wstring(),"Unicode source metadata differs");
        std::filesystem::remove(unicodeSource);NativeLauncher retained(files);require(retained.ready(),"Imported assets still required the source disk");
        const auto metadataPending=std::filesystem::path(files.pack.wstring()+L".json.tmp");std::filesystem::create_directory(metadataPending);
        require(!retained.importDisk(adf)&&bytes(files.pack)==before&&retained.ready(),"Failed provenance write replaced a good pack");std::filesystem::remove(metadataPending);
        const auto locked=CreateFileW(files.pack.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);require(locked!=INVALID_HANDLE_VALUE,"Cannot lock test destination");
        require(!retained.importDisk(adf)&&bytes(files.pack)==before,"Failed atomic replacement changed a good pack");CloseHandle(locked);
        require(bytes(adf)==original,"Failure recovery modified the source ADF");
    }
}
void preferences(const std::filesystem::path& root){
    const auto files=paths(root);const NativeDisplayPreferences original{};require(saveDisplayPreferences(files.settings,original),"Cannot seed settings");
    NativeLauncher staged(files);staged.change(0);staged.change(1);staged.change(2);staged.change(3);
    require(loadDisplayPreferences(files.settings)==original,"Launcher edits saved before Play");
    {NativeLauncher closed(files);require(closed.preferences()==original,"Closing did not discard pending changes");}
    const auto temporary=std::filesystem::path(files.settings.wstring()+L".tmp");std::filesystem::create_directory(temporary);
    require(!staged.saveForPlay()&&loadDisplayPreferences(files.settings)==original&&staged.preferences().interpolate,"Failed save lost staged values or replaced settings");
    std::filesystem::remove(temporary);require(staged.saveForPlay()&&loadDisplayPreferences(files.settings)==staged.preferences(),"Play did not save shared preferences");
    auto faithful=original;faithful.navigation=NativeMenuNavigation::Original;
    staged.resetPreferences();require(staged.preferences()==faithful&&staged.ready(),"Restoring defaults changed assets or did not restore Original navigation");require(staged.saveForPlay(),"Cannot restore original preferences");
    NativeLauncher overridden(files,true,true);require(overridden.preferences().fullscreen&&overridden.preferences().interpolate,"Launch override not visible");
    require(overridden.saveForPlay()&&loadDisplayPreferences(files.settings)==faithful,"Play persisted a launch-only override");
    overridden.change(2);overridden.change(2);require(overridden.saveForPlay()&&loadDisplayPreferences(files.settings).interpolate,"Explicit preference edit did not override launch policy");
    require(defaultNativeLaunchPaths().pack.is_absolute()&&defaultNativeLaunchPaths().recordings.empty(),"Player defaults depend on working directory or enable recording");
}
void recording(const std::filesystem::path& pack,const std::filesystem::path& root){
    ReplayScene normal,recorded;normal.loadNative(pack,{});recorded.loadNative(pack,root/L"explicit-recording");
    require(normal.liveRecording().empty()&&!recorded.liveRecording().empty(),"Recording opt-in differs");
    for(unsigned tick=0;tick<96;++tick){const unsigned control=tick<16?0:17;
        require(normal.stepLive(control)==recorded.stepLive(control),"Recording affected race lifetime");
        require(normal.renderLive(.5)==recorded.renderLive(.5)&&normal.liveTicks()==recorded.liveTicks(),"Recording affected simulation or presentation");}
    for(unsigned tick=0;tick<600&&!normal.liveTerminal();++tick)
        require(normal.stepLive(0,true)==recorded.stepLive(0,true),"Recording affected the original quit gate");
    require(normal.liveTerminal()&&recorded.liveTerminal(),"Recording test did not reach race exit");
    auto normalMemory=std::make_unique<std::array<std::uint8_t,0x80000>>(),recordedMemory=std::make_unique<std::array<std::uint8_t,0x80000>>();
    normal.finishLiveExit(*normalMemory);recorded.finishLiveExit(*recordedMemory);
    require(*normalMemory==*recordedMemory,"Recording changed persistent simulation memory");
    normal.resetLive();require(normal.liveRecording().empty()&&normal.liveTicks()==0,"Reset enabled recording");
}
WindowsLauncher* activeView{};NativeLauncher* activeModel{};bool drawingStatePreserved=true;
LRESULT CALLBACK procedure(HWND window,UINT kind,WPARAM key,LPARAM data){
    if(kind==WM_DRAWITEM&&activeView){
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(data);
        const auto mode=GetBkMode(item.hDC);const auto colour=GetTextColor(item.hDC);const auto background=GetBkColor(item.hDC);
        activeView->drawButton(item);
        drawingStatePreserved&=GetBkMode(item.hDC)==mode&&GetTextColor(item.hDC)==colour&&GetBkColor(item.hDC)==background;
        return TRUE;
    }
    return DefWindowProcW(window,kind,key,data);
}
void snapshot(const std::filesystem::path& root,NativeLauncher& model,int width,int height,const wchar_t* name){
    WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SCRLauncherTest";RegisterClassW(&type);
    const auto window=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Launcher test",WS_POPUP,-30000,-30000,width,height,nullptr,nullptr,type.hInstance,nullptr);
    require(window!=nullptr,"Cannot create launcher test window");
    WindowsLauncher view;activeView=&view;activeModel=&model;view.create(window);view.sync(model);ShowWindow(window,SW_SHOWNOACTIVATE);view.focusInitial(model);
    require(GetFocus()==GetDlgItem(window,model.ready()?LauncherPlay:LauncherAdf),"Initial launcher focus differs");
    require(view.navigate(16,model)==LauncherAction::None,"Held confirm activated launcher");view.navigate(0,model);
    require(view.navigate(16,model)==(model.ready()?LauncherAction::Play:LauncherAction::ChooseAdf),"Fresh controller confirm differs");view.navigate(0,model);
    if(model.ready()){
        SetFocus(GetDlgItem(window,LauncherMotion));const bool previous=model.preferences().interpolate;
        view.navigate(8,model);view.navigate(8,model);require(model.preferences().interpolate!=previous,"Controller setting change or held-input gate differs");view.navigate(0,model);model.resetPreferences();view.sync(model);
    }
    view.focusInitial(model);
    const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels{};const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto previous=SelectObject(dc,bitmap);
    view.paint(dc,width,height,model);
    for(unsigned id=LauncherAdf;id<=LauncherPlay;++id){const auto child=GetDlgItem(window,int(id));RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
        const auto saved=SaveDC(dc);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);RestoreDC(dc,saved);}
    GdiFlush();UINT count{},size{};Gdiplus::GetImageEncodersSize(&count,&size);std::vector<std::uint8_t> encoders(size);auto codec=reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoders.data());Gdiplus::GetImageEncoders(count,size,codec);
    for(UINT n=0;n<count;++n)if(std::wstring(codec[n].MimeType)==L"image/png"){Gdiplus::Bitmap image(bitmap,nullptr);require(image.Save((root/name).c_str(),&codec[n].Clsid,nullptr)==Gdiplus::Ok,"Cannot save launcher preview");}
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(window);activeView=nullptr;activeModel=nullptr;
    require(drawingStatePreserved,"Owner drawing changed the native control's drawing context");
}
}
int main(int argc,char** argv)try{
    if(argc<3||argc>4)throw std::invalid_argument("native_launcher_tests pack output [adf]");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);const std::filesystem::path adf=argc==4?argv[3]:"";
    assets(argv[1],root,adf);preferences(root);recording(argv[1],root);
    ULONG_PTR token{};Gdiplus::GdiplusStartupInput input;require(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"Cannot initialize preview capture");
    {NativeLauncher ready(paths(root));snapshot(root,ready,960,720,L"launcher-ready.png");snapshot(root,ready,740,640,L"launcher-compact.png");
    NativeLauncher missing(paths(root/L"first-run"));snapshot(root,missing,960,720,L"launcher-first-run.png");}
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"{\"status\":\"passed\",\"supported_import\":"<<(adf.empty()?"false":"true")<<",\"atomic_import_failure\":true,\"shared_preferences\":true,\"launch_overrides_transient\":true,\"recording_isolation_ticks\":96,\"recording_final_memory_identical\":true,\"controller_navigation\":true,\"rendered_states\":3}\n";
    return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}

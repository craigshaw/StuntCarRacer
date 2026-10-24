#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "windows_game_display.h"
#include "replay_view.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace scr::viewer {
WindowsGameDisplay::WindowsGameDisplay(){Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&token_,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Cannot initialise display filtering");}
WindowsGameDisplay::~WindowsGameDisplay(){Gdiplus::GdiplusShutdown(token_);}
void WindowsGameDisplay::fullscreen(HWND window,bool enabled){
    if(enabled==fullscreen_)return;
    if(enabled){
        windowStyle_=GetWindowLongPtrW(window,GWL_STYLE);placement_.length=sizeof(placement_);
        if(!GetWindowPlacement(window,&placement_))throw std::runtime_error("Cannot save window placement");
        SetWindowLongPtrW(window,GWL_STYLE,windowStyle_&~WS_OVERLAPPEDWINDOW);fullscreen_=true;monitorChanged(window);
    }else{
        fullscreen_=false;SetWindowLongPtrW(window,GWL_STYLE,windowStyle_);
        SetWindowPlacement(window,&placement_);
        SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
    }
}
void WindowsGameDisplay::monitorChanged(HWND window){
    if(!fullscreen_)return;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))throw std::runtime_error("Cannot find fullscreen monitor");
    const auto& r=monitor.rcMonitor;
    if(!SetWindowPos(window,HWND_TOP,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOOWNERZORDER|SWP_FRAMECHANGED))throw std::runtime_error("Cannot enter fullscreen");
}
void WindowsGameDisplay::apply(HWND window,const NativeDisplayPreferences& preferences,bool forceWindowScale){
    const bool resized=forceWindowScale||!windowedPreferencesApplied_||
        preferences.scaling!=windowedPreferences_.scaling||preferences.integerScale!=windowedPreferences_.integerScale||preferences.view!=windowedPreferences_.view;
    fullscreen(window,preferences.fullscreen);
    if(forceWindowScale&&fullscreen_)monitorChanged(window);
    if(resized&&!fullscreen_&&preferences.scaling==NativeScaling::Integer&&preferences.integerScale){
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))throw std::runtime_error("Cannot find window work area");
        const int sourceWidth=displayWidthAtScale(preferences.view,1);
        RECT extent{0,0,sourceWidth,originalDisplayHeight};
        AdjustWindowRectExForDpi(&extent,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window));
        const int borderWidth=extent.right-extent.left-sourceWidth,borderHeight=extent.bottom-extent.top-originalDisplayHeight;
        const auto& work=monitor.rcWork;
        const auto fitted=displayRectangle(work.right-work.left-borderWidth,work.bottom-work.top-borderHeight,preferences);
        const unsigned maximum=unsigned(std::max(1,fitted.height/originalDisplayHeight));
        const int scale=int(std::min(preferences.integerScale,maximum));
        const int width=displayWidthAtScale(preferences.view,unsigned(scale))+borderWidth,height=originalDisplayHeight*scale+borderHeight;
        if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
        RECT current{};GetWindowRect(window,&current);
        const int x=std::clamp(int(current.left),int(work.left),std::max(int(work.left),int(work.right)-width));
        const int y=std::clamp(int(current.top),int(work.top),std::max(int(work.top),int(work.bottom)-height));
        if(!SetWindowPos(window,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE))throw std::runtime_error("Cannot resize integer-scaled game window");
    }
    if(!fullscreen_){windowedPreferences_=preferences;windowedPreferencesApplied_=true;}
}
void WindowsGameDisplay::beginSettings(HWND window){
    settingsFullscreen_=fullscreen_;settingsWindowedPlacement_=placement_;
    if(!GetWindowPlacement(window,&settingsPlacement_))throw std::runtime_error("Cannot save settings window placement");
}
void WindowsGameDisplay::endSettings(HWND window,bool applied){
    if(applied)return;
    placement_=settingsWindowedPlacement_;
    if(!settingsFullscreen_&&!SetWindowPlacement(window,&settingsPlacement_))throw std::runtime_error("Cannot restore settings window placement");
}
void WindowsGameDisplay::filter(const std::vector<std::uint32_t>& pixels,int width,int height,unsigned sourceWidth){
    const bool resized=filterWidth_!=sourceWidth||columns_.size()!=std::size_t(width)||rows_.size()!=std::size_t(height);
    if(!resized&&filterSource_==pixels)return;
    if(resized){
        const auto axis=[](unsigned source,unsigned target){
            std::vector<Sample> samples;samples.reserve(target);
            for(unsigned n=0;n<target;++n){
                const double coordinate=(double(n)+0.5)*source/target-0.5;const int first=int(std::floor(coordinate));
                samples.push_back({unsigned(std::clamp(first,0,int(source)-1)),unsigned(std::clamp(first+1,0,int(source)-1)),unsigned(std::lround((coordinate-first)*256))});
            }
            return samples;
        };
        filterWidth_=sourceWidth;columns_=axis(sourceWidth,unsigned(width));rows_=axis(200,unsigned(height));
        horizontal_.resize(std::size_t(width)*200);filtered_.resize(std::size_t(width)*height);
    }
    // Separable, fixed-point bilinear filtering. Packed channel arithmetic
    // keeps the full-resolution pass inexpensive; edge samples clamp to the
    // source image so letterboxing never bleeds into the picture.
    const auto blend=[](std::uint32_t a,std::uint32_t b,unsigned weight){
        const auto inverse=256-weight;
        return ((((a&0xff00ff)*inverse+(b&0xff00ff)*weight)>>8)&0xff00ff)|
            ((((a&0x00ff00)*inverse+(b&0x00ff00)*weight)>>8)&0x00ff00);
    };
    for(unsigned y=0;y<200;++y)for(int x=0;x<width;++x){const auto& sample=columns_[x];horizontal_[y*width+x]=blend(pixels[y*sourceWidth+sample.first],pixels[y*sourceWidth+sample.second],sample.weight);}
    for(int y=0;y<height;++y){const auto& sample=rows_[y];const auto* first=horizontal_.data()+sample.first*width;const auto* second=horizontal_.data()+sample.second*width;
        auto* target=filtered_.data()+y*width;for(int x=0;x<width;++x)target[x]=blend(first[x],second[x],sample.weight);
    }
    filterSource_=pixels;
}
void WindowsGameDisplay::paint(HDC dc,int width,int height,const std::vector<std::uint32_t>& pixels,const NativeDisplayPreferences& preferences){
    if(pixels.size()!=320*200&&pixels.size()!=replayWidescreenWidth*200)throw std::invalid_argument("Invalid native display canvas");
    const int sourceWidth=int(pixels.size()/200);
    RECT client{0,0,width,height};FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const auto fit=displayRectangle(width,height,preferences,sourceWidth);
    if(fit.width<=0||fit.height<=0)return;
    // The approved 432-column working frame slightly overdraws the exact 16:9
    // aperture. Crop only its outer edges, preserving the original car scale.
    auto canvas=fit;
    if(sourceWidth==replayWidescreenWidth){canvas.width=(fit.width*81+40)/80;canvas.x=fit.x+(fit.width-canvas.width)/2;}
    const auto saved=SaveDC(dc);IntersectClipRect(dc,fit.x,fit.y,fit.x+fit.width,fit.y+fit.height);
    const bool smooth=preferences.scaling!=NativeScaling::Integer&&preferences.filtering==NativeFiltering::Bilinear;
    if(smooth)filter(pixels,canvas.width,canvas.height,unsigned(sourceWidth));
    {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=sourceWidth;
        info.bmiHeader.biHeight=-200;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        if(smooth){info.bmiHeader.biWidth=canvas.width;info.bmiHeader.biHeight=-canvas.height;}
        SetStretchBltMode(dc,COLORONCOLOR);
        const auto drawn=StretchDIBits(dc,canvas.x,canvas.y,canvas.width,canvas.height,0,0,smooth?canvas.width:sourceWidth,smooth?canvas.height:200,
            smooth?filtered_.data():pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        RestoreDC(dc,saved);
        if(drawn==GDI_ERROR||!drawn)throw std::runtime_error("Cannot scale native game image");
    }
}
void WindowsGameDisplay::fpsOverlay(HDC dc,int width,int height,double framesPerSecond){
    const double scale=std::clamp(std::min(double(width)/960,double(height)/720),0.75,1.5);
    const auto px=[&](int value){return int(std::lround(value*scale));};
    std::wostringstream label;label<<std::fixed<<std::setprecision(1)<<framesPerSecond<<L" FPS";
    const auto value=label.str();const auto saved=SaveDC(dc);
    const auto font=CreateFontW(-px(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
    SelectObject(dc,font);
    const int margin=px(8),padding=px(6);
    SetTextColor(dc,RGB(241,242,235));SetBkMode(dc,TRANSPARENT);
    TextOutW(dc,margin+padding,margin+padding,value.c_str(),int(value.size()));
    RestoreDC(dc,saved);DeleteObject(font);
}
void WindowsGameDisplay::overlay(HDC dc,int width,int height,const NativeDisplaySettings& settings,const std::wstring& error){
    if(!settings.blocksGame())return;
    {Gdiplus::Graphics graphics(dc);Gdiplus::SolidBrush dim(Gdiplus::Color(176,0,0,0));graphics.FillRectangle(&dim,0,0,width,height);}
    const double scale=std::max(0.3,std::min({1.5,double(width)/800,double(height)/540}));
    const auto px=[&](int n){return int(std::lround(n*scale));};
    const bool menu=settings.page()==NativeDisplayPage::Settings;
    const bool confirmation=settings.page()==NativeDisplayPage::QuitConfirmation;
    const int help=menu?406:confirmation?262:64+int(settings.optionCount())*36+18;
    const int panelWidth=px(560),panelHeight=px(menu?538:confirmation?334:help+94),left=(width-panelWidth)/2,top=(height-panelHeight)/2;
    const auto fill=[&](RECT r,COLORREF colour){const auto brush=CreateSolidBrush(colour);FillRect(dc,&r,brush);DeleteObject(brush);};
    RECT panel{left,top,left+panelWidth,top+panelHeight};fill(panel,RGB(19,25,35));
    const auto border=CreateSolidBrush(RGB(78,103,130));FrameRect(dc,&panel,border);DeleteObject(border);
    const auto font=CreateFontW(-px(19),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
    const auto text=[&](int x,int y,const wchar_t* value,COLORREF colour){SetTextColor(dc,colour);TextOutW(dc,left+px(x),top+px(y),value,int(wcslen(value)));};
    text(24,18,menu?L"Settings":confirmation?L"Quit to Desktop?":L"Game menu",RGB(245,247,250));
    const wchar_t* actions[]{L"Resume",L"Settings",settings.canLeaveRace()?L"Leave Race":L"Quit to Desktop",L"Quit to Desktop"};
    const wchar_t* confirmActions[]{L"Cancel",L"Quit to Desktop"};
    if(confirmation){
        const wchar_t* message=L"Close Stunt Car Racer?";
        if(settings.quitContext()==NativeQuitContext::Career)message=L"Your career and Hall of Fame will be saved before closing.";
        else if(settings.quitContext()==NativeQuitContext::CareerRace)message=L"Your career will be saved.\nThis unfinished race will restart from its matchup.";
        else if(settings.quitContext()==NativeQuitContext::Multiplayer)message=L"Progress since your last manual save will be lost.";
        RECT warning{left+px(24),top+px(64),left+panelWidth-px(24),top+px(150)};SetTextColor(dc,RGB(167,180,197));
        DrawTextW(dc,message,-1,&warning,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
    }
    const auto& prefs=settings.preferences();
    const wchar_t* labels[]{L"Display mode",L"Image sizing",prefs.scaling==NativeScaling::Integer?L"Integer scale":L"Filtering",L"60 FPS smoothing",L"Driving controls",L"Menu navigation",L"Aspect ratio",L"Apply and back",L"Cancel"};
    auto detail=prefs.scaling==NativeScaling::Integer?integerScaleName(prefs.integerScale):std::wstring(filteringName(prefs.filtering));
    if(prefs.scaling==NativeScaling::Integer){
        const auto actual=unsigned(displayRectangle(width,height,prefs).height/originalDisplayHeight);
        if(!prefs.integerScale)detail+=L" ("+integerScaleName(actual)+L")";
        else if(actual!=prefs.integerScale)detail+=L" (using "+integerScaleName(actual)+L")";
    }
    const wchar_t* values[]{prefs.fullscreen?L"Borderless fullscreen":L"Windowed",scalingName(prefs.scaling),detail.c_str(),prefs.interpolate?L"On":L"Off",
        prefs.controls==DriveControlPreset::Original?L"Original":L"Modern",prefs.navigation==NativeMenuNavigation::Responsive?L"Responsive":L"Original",viewName(prefs.view)};
    for(unsigned n=0;n<settings.optionCount();++n){
        const int y=(confirmation?166:64)+int(n)*36;
        if(n==settings.selected())fill({left+px(16),top+px(y-3),left+panelWidth-px(16),top+px(y+30)},RGB(35,65,91));
        text(28,y,menu?labels[n]:confirmation?confirmActions[n]:actions[n],RGB(237,242,247));
        if(menu&&n<7)text(250,y,values[n],RGB(148,213,243));
    }
    text(24,help,menu?L"Arrows / D-pad: select and change":L"Arrows / D-pad: select",RGB(167,180,197));
    text(24,help+25,confirmation?L"Enter / A: confirm    Esc / B: cancel":L"Enter / A: confirm    Esc / B: back",RGB(167,180,197));
    if(menu)text(24,help+50,prefs.controls==DriveControlPreset::Modern?L"Hold RT: accel   LT: brake   A / RB: boost":L"Stick / D-pad: accel/brake   A / RB: boost",RGB(148,213,243));
    if(!confirmation)text(24,help+(menu?75:50),error.empty()?(menu?L"Preview changes; Apply saves your choices.":L"Esc / P / Start: resume    F11: fullscreen"):error.c_str(),error.empty()?RGB(167,180,197):RGB(255,170,130));
    SelectObject(dc,old);DeleteObject(font);
}
} // namespace scr::viewer

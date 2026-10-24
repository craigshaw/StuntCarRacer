#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "windows_game_display.h"
#include "replay_view.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
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
void WindowsGameDisplay::filter(const std::vector<std::uint32_t>& pixels,int width,int height){
    const bool resized=columns_.size()!=std::size_t(width)||rows_.size()!=std::size_t(height);
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
        columns_=axis(320,unsigned(width));rows_=axis(200,unsigned(height));
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
    for(unsigned y=0;y<200;++y)for(int x=0;x<width;++x){const auto& sample=columns_[x];horizontal_[y*width+x]=blend(pixels[y*320+sample.first],pixels[y*320+sample.second],sample.weight);}
    for(int y=0;y<height;++y){const auto& sample=rows_[y];const auto* first=horizontal_.data()+sample.first*width;const auto* second=horizontal_.data()+sample.second*width;
        auto* target=filtered_.data()+y*width;for(int x=0;x<width;++x)target[x]=blend(first[x],second[x],sample.weight);
    }
    filterSource_=pixels;
}
void WindowsGameDisplay::paint(HDC dc,int width,int height,const std::vector<std::uint32_t>& pixels,NativeScaling scaling){
    if(pixels.size()!=64000)throw std::invalid_argument("Native display requires a 320 by 200 image");
    RECT client{0,0,width,height};FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const auto fit=fitPresentationImage(320,200,width,height,scaling==NativeScaling::Integer);
    if(fit.width<=0||fit.height<=0)return;
    const bool smooth=scaling==NativeScaling::FitSmooth;
    if(smooth)filter(pixels,fit.width,fit.height);
    {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=320;
        info.bmiHeader.biHeight=-200;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        if(smooth){info.bmiHeader.biWidth=fit.width;info.bmiHeader.biHeight=-fit.height;}
        SetStretchBltMode(dc,COLORONCOLOR);
        const auto drawn=StretchDIBits(dc,fit.x,fit.y,fit.width,fit.height,0,0,smooth?fit.width:320,smooth?fit.height:200,
            smooth?filtered_.data():pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        if(drawn==GDI_ERROR||!drawn)throw std::runtime_error("Cannot scale native game image");
    }
}
void WindowsGameDisplay::overlay(HDC dc,int width,int height,const NativeDisplaySettings& settings,const std::wstring& error){
    if(!settings.blocksGame())return;
    {Gdiplus::Graphics graphics(dc);Gdiplus::SolidBrush dim(Gdiplus::Color(176,0,0,0));graphics.FillRectangle(&dim,0,0,width,height);}
    const double scale=std::max(0.5,std::min({1.5,double(width)/800,double(height)/500}));
    const auto px=[&](int n){return int(std::lround(n*scale));};
    const bool menu=settings.page()==NativeDisplayPage::Settings;
    const int panelWidth=px(560),panelHeight=px(menu?466:240),left=(width-panelWidth)/2,top=(height-panelHeight)/2;
    const auto fill=[&](RECT r,COLORREF colour){const auto brush=CreateSolidBrush(colour);FillRect(dc,&r,brush);DeleteObject(brush);};
    RECT panel{left,top,left+panelWidth,top+panelHeight};fill(panel,RGB(19,25,35));
    const auto border=CreateSolidBrush(RGB(78,103,130));FrameRect(dc,&panel,border);DeleteObject(border);
    const auto font=CreateFontW(-px(19),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
    const auto text=[&](int x,int y,const wchar_t* value,COLORREF colour){SetTextColor(dc,colour);TextOutW(dc,left+px(x),top+px(y),value,int(wcslen(value)));};
    text(24,18,menu?L"Settings":L"Paused",RGB(245,247,250));
    const wchar_t* labels[]{L"Display mode",L"Scaling",L"60 FPS smoothing",L"Driving controls",L"Menu navigation",L"Apply and back",L"Cancel"};
    const wchar_t* pause[]{L"Resume",L"Settings"};
    const auto& prefs=settings.preferences();
    const wchar_t* values[]{prefs.fullscreen?L"Borderless fullscreen":L"Windowed",scalingName(prefs.scaling),prefs.interpolate?L"On":L"Off",
        prefs.controls==DriveControlPreset::Original?L"Original":L"Modern",prefs.navigation==NativeMenuNavigation::Responsive?L"Responsive":L"Original"};
    for(unsigned n=0;n<(menu?7u:2u);++n){
        const int y=64+int(n)*36;
        if(n==settings.selected())fill({left+px(16),top+px(y-3),left+panelWidth-px(16),top+px(y+30)},RGB(35,65,91));
        text(28,y,menu?labels[n]:pause[n],RGB(237,242,247));
        if(menu&&n<5)text(250,y,values[n],RGB(148,213,243));
    }
    const int help=menu?334:154;
    text(24,help,L"Arrows / D-pad: select and change",RGB(167,180,197));
    text(24,help+25,L"Enter / A: confirm    Esc / B: back",RGB(167,180,197));
    if(menu)text(24,help+50,prefs.controls==DriveControlPreset::Modern?L"Hold RT: accel   LT: brake   A / RB: boost":L"Stick / D-pad: accel/brake   A / RB: boost",RGB(148,213,243));
    text(24,help+(menu?75:50),error.empty()?(menu?L"Preview changes; Apply saves your choices.":L"F10: settings    F11: fullscreen"):error.c_str(),error.empty()?RGB(167,180,197):RGB(255,170,130));
    SelectObject(dc,old);DeleteObject(font);
}
} // namespace scr::viewer

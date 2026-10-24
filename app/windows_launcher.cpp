#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "windows_launcher.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace scr::viewer {
namespace {
constexpr COLORREF background=RGB(16,21,28),panel=RGB(28,35,44),ink=RGB(241,242,235),muted=RGB(172,184,197),line=RGB(53,65,79),amber=RGB(239,186,75);
void fill(HDC dc,RECT rect,COLORREF colour){const auto brush=CreateSolidBrush(colour);FillRect(dc,&rect,brush);DeleteObject(brush);}
void text(HDC dc,HFONT font,RECT rect,const std::wstring& value,COLORREF colour,unsigned flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
    const auto previous=SelectObject(dc,font);SetTextColor(dc,colour);SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,value.c_str(),int(value.size()),&rect,flags|DT_NOPREFIX);SelectObject(dc,previous);
}
LRESULT CALLBACK controlProcedure(HWND child,UINT message,WPARAM key,LPARAM data,UINT_PTR,DWORD_PTR){
    if((message==WM_PAINT||message==WM_PRINT||message==WM_PRINTCLIENT)&&GetDlgCtrlID(child)>=LauncherDisplay&&GetDlgCtrlID(child)<=LauncherNavigation){
        const auto result=DefSubclassProc(child,message,key,data);const auto dc=message==WM_PAINT?GetDC(child):reinterpret_cast<HDC>(key);RECT rect{};GetClientRect(child,&rect);
        // Keep the native combobox's accessibility and popup, while matching
        // its closed border and arrow to the launcher surface.
        for(unsigned n=0;n<3;++n){const auto brush=CreateSolidBrush(n?panel:GetFocus()==child?amber:line);FrameRect(dc,&rect,brush);DeleteObject(brush);InflateRect(&rect,-1,-1);}
        RECT arrow=rect;arrow.left=arrow.right-24;fill(dc,arrow,panel);
        const auto pen=CreatePen(PS_SOLID,1,muted);const auto old=SelectObject(dc,pen);const int x=(arrow.left+arrow.right)/2,y=(arrow.top+arrow.bottom)/2;
        MoveToEx(dc,x-4,y-2,nullptr);LineTo(dc,x,y+2);LineTo(dc,x+4,y-2);SelectObject(dc,old);DeleteObject(pen);if(message==WM_PAINT)ReleaseDC(child,dc);return result;
    }
    if(message==WM_KEYDOWN||message==WM_KEYUP){
        const auto id=GetDlgCtrlID(child);
        const bool combo=id>=LauncherDisplay&&id<=LauncherNavigation;
        if(combo&&SendMessageW(child,CB_GETDROPPEDSTATE,0,0))return DefSubclassProc(child,message,key,data);
        unsigned nav=key==VK_UP?1:key==VK_DOWN?2:key==VK_LEFT?4:key==VK_RIGHT?8:(key==VK_RETURN||key==VK_SPACE)?16:key==VK_ESCAPE?32:0;
        if(nav){if(message==WM_KEYDOWN&&(data&0x40000000))return 0;if(message==WM_KEYUP)nav=0;SendMessageW(GetParent(child),WindowsLauncher::navigationMessage,nav,0);return 0;}
    }
    return DefSubclassProc(child,message,key,data);
}
unsigned valueAt(const NativeDisplayPreferences& value,unsigned row){
    return row==0?unsigned(value.fullscreen):row==1?unsigned(value.scaling):row==2?unsigned(value.interpolate):
        row==3?unsigned(value.controls==DriveControlPreset::Modern):unsigned(value.navigation==NativeMenuNavigation::Original);
}
}
struct WindowsLauncher::Artwork {
    ULONG_PTR token{};IStream* stream{};std::unique_ptr<Gdiplus::Image> image;
    Artwork(){
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return;
        const auto module=GetModuleHandleW(nullptr);const auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));
        if(!resource)return;
        const auto size=SizeofResource(module,resource);const auto loaded=LoadResource(module,resource);const auto bytes=LockResource(loaded);
        if(!size||!bytes)return;
        const auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)return;const auto target=GlobalLock(memory);
        if(!target){GlobalFree(memory);return;}std::memcpy(target,bytes,size);GlobalUnlock(memory);
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return;}
        image.reset(Gdiplus::Image::FromStream(stream));
        if(!image||image->GetLastStatus()!=Gdiplus::Ok)image.reset();
    }
    ~Artwork(){image.reset();if(stream)stream->Release();if(token)Gdiplus::GdiplusShutdown(token);}
};
WindowsLauncher::WindowsLauncher():artwork_(std::make_unique<Artwork>()),inputBrush_(CreateSolidBrush(background)){}
WindowsLauncher::~WindowsLauncher(){for(auto font:fonts_)if(font)DeleteObject(font);DeleteObject(inputBrush_);}
void WindowsLauncher::create(HWND parent){
    parent_=parent;
    for(unsigned n=0;n<controls_.size();++n){
        const auto id=LauncherAdf+n;const bool combo=id>=LauncherDisplay&&id<=LauncherNavigation;
        controls_[n]=CreateWindowExW(0,combo?L"COMBOBOX":L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|
            (combo?(CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL):BS_OWNERDRAW),0,0,10,10,parent,
            reinterpret_cast<HMENU>(UINT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        if(!controls_[n])throw std::runtime_error("Cannot create launcher controls.");
        SetWindowSubclass(controls_[n],controlProcedure,1,0);
    }
    const wchar_t* choices[][3]{{L"Windowed",L"Borderless fullscreen",nullptr},{L"Integer",L"Fit sharp",L"Fit smooth"},
        {L"Original updates",L"Smooth interpolation",nullptr},{L"Original",L"Modern",nullptr},{L"Responsive",L"Original",nullptr}};
    for(unsigned row=0;row<5;++row)for(const auto choice:choices[row])if(choice)SendMessageW(controls_[row+2],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));
    SetWindowTextW(controls_[1],L"Details");SetWindowTextW(controls_[7],L"Restore original defaults");
    SetWindowTextW(controls_[8],L"Quit");SetWindowTextW(controls_[9],L"Play");
    layout(parent);
}
void WindowsLauncher::visible(bool enabled){visible_=enabled;for(const auto control:controls_)if(control)ShowWindow(control,enabled?SW_SHOW:SW_HIDE);}
void WindowsLauncher::rebuildFonts(){
    for(auto& font:fonts_){if(font)DeleteObject(font);font=nullptr;}
    const int heights[]{42,16,13,15};
    for(unsigned n=0;n<fonts_.size();++n)fonts_[n]=CreateFontW(-int(std::lround(heights[n]*scale_)),0,0,0,n==0?FW_HEAVY:n==1?FW_SEMIBOLD:FW_NORMAL,
        n==0,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(const auto control:controls_)if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(fonts_[3]),FALSE);
}
void WindowsLauncher::layout(HWND window){
    if(!parent_)return;RECT client{};GetClientRect(window,&client);width_=client.right;height_=client.bottom;
    const auto dpi=GetDpiForWindow(window);const double next=std::max(0.65,std::min({double(dpi)/96,double(width_)/820,double(height_)/680}));
    if(fonts_[0]==nullptr||std::abs(next-scale_)>0.01){scale_=next;rebuildFonts();}
    const auto px=[&](int value){return int(std::lround(value*scale_));};
    margin_=px(40);header_=px(145);assetY_=header_;assetHeight_=px(82);footerY_=height_-px(82);
    settingsY_=assetY_+assetHeight_+px(details_?106:39);
    rowHeight_=std::max(px(43),(footerY_-px(98)-settingsY_)/5);
    const int right=width_-margin_,comboWidth=px(245),buttonHeight=px(40);
    MoveWindow(controls_[0],right-px(148),assetY_+px(21),px(130),buttonHeight,TRUE);
    MoveWindow(controls_[1],margin_,assetY_+assetHeight_+px(5),px(78),px(28),TRUE);
    for(unsigned n=0;n<5;++n){MoveWindow(controls_[n+2],right-comboWidth,settingsY_+int(n)*rowHeight_+px(8),comboWidth,px(210),TRUE);
        SendMessageW(controls_[n+2],CB_SETITEMHEIGHT,WPARAM(-1),px(33));SendMessageW(controls_[n+2],CB_SETITEMHEIGHT,0,px(32));}
    MoveWindow(controls_[7],margin_,settingsY_+5*rowHeight_+px(5),px(200),px(28),TRUE);
    MoveWindow(controls_[8],right-px(224),footerY_+px(21),px(82),buttonHeight,TRUE);
    MoveWindow(controls_[9],right-px(130),footerY_+px(21),px(130),buttonHeight,TRUE);
    InvalidateRect(window,nullptr,FALSE);
}
void WindowsLauncher::sync(const NativeLauncher& model){
    if(!parent_)return;
    SetWindowTextW(controls_[0],model.ready()?L"Change ADF…":L"Choose ADF…");EnableWindow(controls_[9],model.ready());
    for(unsigned row=0;row<5;++row)SendMessageW(controls_[row+2],CB_SETCURSEL,valueAt(model.preferences(),row),0);
    InvalidateRect(parent_,nullptr,FALSE);for(const auto control:controls_)InvalidateRect(control,nullptr,FALSE);
}
void WindowsLauncher::focusInitial(const NativeLauncher& model){SetFocus(controls_[model.ready()?9:0]);previous_=~0u;}
void WindowsLauncher::paint(HDC dc,int width,int height,const NativeLauncher& model){
    fill(dc,{0,0,width,height},background);
    const auto px=[&](int value){return int(std::lround(value*scale_));};
    if(artwork_->image){
        Gdiplus::Graphics graphics(dc);graphics.SetClip(Gdiplus::Rect(0,0,width,header_));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const auto imageWidth=artwork_->image->GetWidth(),imageHeight=artwork_->image->GetHeight();
        const int padding=px(12);
        const double fit=std::min(double(std::max(1,width-2*margin_))/imageWidth,
            double(std::max(1,header_-2*padding))/imageHeight);
        const int w=std::max(1,int(std::lround(imageWidth*fit))),h=std::max(1,int(std::lround(imageHeight*fit)));
        Gdiplus::ColorMatrix matrix{{{1,0,0,0,0},{0,1,0,0,0},{0,0,1,0,0},{0,0,0,0.24f,0},{0,0,0,0,1}}};
        Gdiplus::ImageAttributes attributes;attributes.SetColorMatrix(&matrix);
        graphics.DrawImage(artwork_->image.get(),Gdiplus::Rect(width-margin_-w,(header_-h)/2,w,h),0,0,int(imageWidth),int(imageHeight),Gdiplus::UnitPixel,&attributes);
        Gdiplus::LinearGradientBrush fade(Gdiplus::Point(0,0),Gdiplus::Point(0,header_),Gdiplus::Color(35,16,21,28),Gdiplus::Color(255,16,21,28));
        graphics.FillRectangle(&fade,0,0,width,header_);
    }
    text(dc,fonts_[0],{margin_,px(48),width-margin_,px(110)},L"STUNT CAR RACER",ink);
    const int right=width-margin_;
    fill(dc,{margin_,assetY_,right,assetY_+assetHeight_},panel);
    text(dc,fonts_[1],{margin_+px(20),assetY_+px(14),right-px(170),assetY_+px(42)},model.ready()?L"✓  Game data ready":L"Original game data needed",model.ready()?RGB(146,211,168):ink);
    text(dc,fonts_[2],{margin_+px(20),assetY_+px(44),right-px(170),assetY_+px(68)},model.ready()?model.sourceName():L"Choose your supported Amiga ADF to get started.",muted,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(details_){
        text(dc,fonts_[2],{margin_,assetY_+assetHeight_+px(33),right,settingsY_-px(20)},
            L"Supported ADF SHA-256: 548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06\nGame data: "+model.paths().pack.wstring(),muted,DT_LEFT|DT_TOP|DT_WORDBREAK);
    }
    const wchar_t* labels[]{L"Display mode",L"Scaling",L"Motion",L"Driving controls",L"Menu navigation"};
    const auto& prefs=model.preferences();
    const wchar_t* scalingHelp=prefs.scaling==NativeScaling::Integer?L"Whole-pixel enlargement with crisp original edges.":prefs.scaling==NativeScaling::FitSharp?L"Fit the display, keeping crisp pixel edges.":L"Fit the display with softer, filtered edges.";
    const wchar_t* help[]{L"Fullscreen takes effect when you press Play.",scalingHelp,prefs.interpolate?L"Smoother motion with a small presentation delay.":L"Original, stepped motion.",
        prefs.controls==DriveControlPreset::Original?L"Original throttle stays on until you brake.":L"Hold gas to accelerate; release to coast. Controller RT / LT.",
        prefs.navigation==NativeMenuNavigation::Responsive?L"Immediate response. Esc / B goes back.":L"Original menu timing and navigation."};
    for(unsigned n=0;n<5;++n){const int y=settingsY_+int(n)*rowHeight_;
        if(n)fill(dc,{margin_,y,right,y+1},line);
        text(dc,fonts_[1],{margin_,y+px(5),right-px(265),y+px(29)},labels[n],ink);
        text(dc,fonts_[2],{margin_,y+px(29),right-px(265),y+rowHeight_},help[n],muted,DT_LEFT|DT_TOP|DT_WORDBREAK);
    }
    if(!model.error().empty())text(dc,fonts_[2],{margin_,footerY_-px(52),right,footerY_-px(5)},model.error(),RGB(255,177,156),DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
    fill(dc,{0,footerY_,width,height},panel);fill(dc,{0,footerY_,width,footerY_+1},line);
    text(dc,fonts_[2],{margin_,footerY_+px(17),right-px(250),footerY_+px(42)},L"Choices are saved when you press Play.",muted);
    text(dc,fonts_[2],{margin_,footerY_+px(40),right-px(250),footerY_+px(66)},L"Arrows / D-pad: navigate   Enter / A: confirm",muted);
}
void WindowsLauncher::drawButton(const DRAWITEMSTRUCT& item){
    const auto dc=item.hDC;const bool combo=item.CtlID>=LauncherDisplay&&item.CtlID<=LauncherNavigation;
    const bool selected=(item.itemState&ODS_SELECTED)!=0,disabled=(item.itemState&ODS_DISABLED)!=0;
    const bool focus=(item.itemState&ODS_FOCUS)!=0;
    const bool primary=item.CtlID==LauncherPlay;
    const bool quiet=item.CtlID==LauncherDetails||item.CtlID==LauncherReset;
    fill(dc,item.rcItem,primary?(disabled?RGB(84,76,54):amber):selected?RGB(46,57,70):quiet?background:panel);
    if(!quiet&&!combo){const auto brush=CreateSolidBrush(focus?amber:line);FrameRect(dc,&item.rcItem,brush);DeleteObject(brush);}
    std::wstring label;
    if(combo){int index=int(item.itemID);if(index<0)index=int(SendMessageW(item.hwndItem,CB_GETCURSEL,0,0));
        if(index>=0){const auto size=SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,index,0);if(size>=0&&size<256){label.resize(std::size_t(size)+1);SendMessageW(item.hwndItem,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(label.data()));label.resize(std::size_t(size));}}}
    else{wchar_t buffer[128]{};GetWindowTextW(item.hwndItem,buffer,128);label=buffer;}
    RECT rect=item.rcItem;rect.left+=int(10*scale_);rect.right-=int(10*scale_);
    text(dc,quiet?fonts_[2]:fonts_[3],rect,label,primary?RGB(28,25,19):disabled?muted:ink,DT_VCENTER|DT_SINGLELINE|(combo||quiet?DT_LEFT:DT_CENTER));
    if(focus&&quiet){const auto brush=CreateSolidBrush(amber);FrameRect(dc,&item.rcItem,brush);DeleteObject(brush);}
}
HBRUSH WindowsLauncher::controlColour(HDC dc){SetTextColor(dc,ink);SetBkColor(dc,background);return inputBrush_;}
LauncherAction WindowsLauncher::command(unsigned id,NativeLauncher& model){
    if(id==LauncherAdf)return LauncherAction::ChooseAdf;
    if(id==LauncherPlay)return model.ready()?LauncherAction::Play:LauncherAction::None;
    if(id==LauncherQuit)return LauncherAction::Quit;
    if(id==LauncherDetails){details_=!details_;SetWindowTextW(controls_[1],details_?L"Hide details":L"Details");layout(parent_);}
    else if(id==LauncherReset)model.resetPreferences();
    else if(id>=LauncherDisplay&&id<=LauncherNavigation){const unsigned row=id-LauncherDisplay;const auto chosen=SendMessageW(controls_[row+2],CB_GETCURSEL,0,0);
        for(unsigned n=0;n<3&&chosen>=0&&valueAt(model.preferences(),row)!=unsigned(chosen);++n)model.change(row);}
    sync(model);return LauncherAction::None;
}
void WindowsLauncher::selectFocus(int direction,const NativeLauncher& model){
    const auto focus=GetFocus();int current=-1;
    for(unsigned n=0;n<controls_.size();++n)if(controls_[n]==focus)current=int(n);
    for(unsigned n=0;n<controls_.size();++n){current=(current+int(controls_.size())+direction)%int(controls_.size());
        if(current!=9||model.ready()){SetFocus(controls_[current]);return;}}
}
LauncherAction WindowsLauncher::navigate(unsigned navigation,NativeLauncher& model){
    const unsigned pressed=navigation&~previous_;previous_=navigation;
    if(!pressed)return LauncherAction::None;
    if(pressed&32)return LauncherAction::Quit;
    if(pressed&1)selectFocus(-1,model);else if(pressed&2)selectFocus(1,model);
    const auto id=unsigned(GetDlgCtrlID(GetFocus()));
    if(id>=LauncherDisplay&&id<=LauncherNavigation&&(pressed&(4|8|16))){model.change(id-LauncherDisplay,(pressed&4)?-1:1);sync(model);return LauncherAction::None;}
    if(pressed&16)return command(id,model);
    return LauncherAction::None;
}
} // namespace scr::viewer

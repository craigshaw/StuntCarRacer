#pragma once
#include <windows.h>
#include "native_display_settings.h"
#include <cstdint>
#include <vector>

namespace scr::viewer {
class WindowsGameDisplay {
public:
    WindowsGameDisplay();
    ~WindowsGameDisplay();
    WindowsGameDisplay(const WindowsGameDisplay&)=delete;
    WindowsGameDisplay& operator=(const WindowsGameDisplay&)=delete;
    void fullscreen(HWND,bool);
    void monitorChanged(HWND);
    bool isFullscreen()const noexcept{return fullscreen_;}
    void paint(HDC,int width,int height,const std::vector<std::uint32_t>&,NativeScaling);
    void overlay(HDC,int width,int height,const NativeDisplaySettings&,const std::wstring& error);
    void fpsOverlay(HDC,int width,int height,double framesPerSecond);
private:
    ULONG_PTR token_{};
    bool fullscreen_{};
    LONG_PTR windowStyle_{};
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
    struct Sample {unsigned first,second,weight;};
    std::vector<Sample> columns_,rows_;
    std::vector<std::uint32_t> horizontal_,filtered_,filterSource_;
    void filter(const std::vector<std::uint32_t>&,int width,int height);
};
} // namespace scr::viewer

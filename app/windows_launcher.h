#pragma once
#include <windows.h>
#include "native_launcher.h"
#include <array>
#include <memory>

namespace scr::viewer {
// Stable native control IDs also form the host integration-test seam.
enum LauncherControl : unsigned { LauncherAdf=1001,LauncherDetails,LauncherDisplay,LauncherScaling,
    LauncherMotion,LauncherControls,LauncherNavigation,LauncherReset,LauncherQuit,LauncherPlay,LauncherScaleDetail };
enum class LauncherAction { None, ChooseAdf, Play, Quit };
class WindowsLauncher {
public:
    WindowsLauncher();
    ~WindowsLauncher();
    void create(HWND);
    void visible(bool);
    void sync(const NativeLauncher&);
    void layout(HWND);
    void paint(HDC,int,int,const NativeLauncher&);
    void drawButton(const DRAWITEMSTRUCT&);
    HBRUSH controlColour(HDC);
    LauncherAction command(unsigned,NativeLauncher&);
    LauncherAction navigate(unsigned,NativeLauncher&);
    void releaseNavigation()noexcept{previous_=~0u;}
    void focusInitial(const NativeLauncher&);
    static constexpr UINT navigationMessage=WM_APP+32;
private:
    struct Artwork;
    std::unique_ptr<Artwork> artwork_;
    HWND parent_{};
    std::array<HWND,11> controls_{};
    std::array<HFONT,4> fonts_{};
    HBRUSH inputBrush_{};
    bool details_{},visible_{true};
    unsigned previous_{~0u};
    double scale_{1};
    int width_{},height_{},margin_{},header_{},assetY_{},assetHeight_{},settingsY_{},rowHeight_{},footerY_{};
    void rebuildFonts();
    void selectFocus(int,const NativeLauncher&);
};
} // namespace scr::viewer

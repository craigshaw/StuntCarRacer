#pragma once
#include "native_display_settings.h"
#include <optional>

namespace scr::viewer {
struct NativeLaunchPaths {
    std::filesystem::path pack,settings,save,recordings;
};
NativeLaunchPaths defaultNativeLaunchPaths();
std::wstring launcherErrorText(const std::exception&);

// Host setup owns no game state and never advances simulation clocks.
class NativeLauncher {
public:
    NativeLauncher(NativeLaunchPaths,std::optional<bool> fullscreen={},std::optional<bool> interpolate={});
    const NativeLaunchPaths& paths()const noexcept{return paths_;}
    const NativeDisplayPreferences& preferences()const noexcept{return pending_;}
    bool ready()const noexcept{return ready_;}
    const std::wstring& sourceName()const noexcept{return source_;}
    const std::wstring& error()const noexcept{return error_;}
    void refreshAssets();
    bool importDisk(const std::filesystem::path&);
    void change(unsigned row,int direction=1);
    void resetPreferences();
    bool saveForPlay();
    void failedStart(const std::exception&);
private:
    NativeLaunchPaths paths_;
    NativeDisplayPreferences saved_,pending_;
    std::optional<bool> fullscreenOverride_,interpolationOverride_;
    unsigned edited_{};
    bool ready_{};
    std::wstring source_,error_;
};
} // namespace scr::viewer

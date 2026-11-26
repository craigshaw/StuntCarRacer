#pragma once
#include "launcher/native_display_settings.h"
#include "persistence/player_data_layout.h"
#include <optional>
#include <string>
#include <vector>

namespace scr::runtime {
struct NativeLaunchPaths {
    std::filesystem::path pack,settings,recordings,dataRoot;
};
NativeLaunchPaths defaultNativeLaunchPaths();
std::filesystem::path nativeExecutablePath();
struct NativeLaunchOptions {
    NativeLaunchPaths paths;
    std::optional<bool> interpolate,fullscreen;
    std::optional<PlayerDataLayout> dataLayout;
};
// Arguments exclude argv[0]. One data root owns every player file.
NativeLaunchOptions parseNativeLaunchOptions(const NativeLaunchPaths& defaults,
    const std::vector<std::wstring>& arguments);
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
    bool ready_{},defaultsApproved_{};
    std::wstring source_,error_;
};
} // namespace scr::runtime

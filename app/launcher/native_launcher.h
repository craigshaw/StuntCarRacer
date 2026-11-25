#pragma once
#include "launcher/native_display_settings.h"
#include "persistence/player_data_layout.h"
#include "persistence/player_data_import.h"
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace scr::runtime {
struct NativeLaunchPaths {
    std::filesystem::path pack,settings,recordings,dataRoot;
};
NativeLaunchPaths defaultNativeLaunchPaths();
std::filesystem::path nativeExecutablePath();
PlayerImportSource legacyPlayerImportSource();
struct NativeLaunchOptions {
    NativeLaunchPaths paths;
    std::optional<bool> interpolate,fullscreen;
    std::optional<PlayerDataLayout> dataLayout;
    bool explicitDataRoot{};
    std::optional<std::filesystem::path> importFrom,importHallFrom;
};
// Arguments exclude argv[0]. One data root owns every player file.
NativeLaunchOptions parseNativeLaunchOptions(const NativeLaunchPaths& defaults,
    const std::vector<std::wstring>& arguments);
std::wstring launcherErrorText(const std::exception&);
enum class LauncherImportPage {None,Review,Working,Report};

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
    void configureDataImport(bool discoverLegacy,std::optional<PlayerImportSource> explicitSource={});
    void openDataImport();
    void importFolder(const std::filesystem::path&,bool hall=false);
    void changeImportOption(unsigned);
    void applyDataImport();
    void freshData();
    void closeDataImport();
    bool pollDataImport();
    LauncherImportPage importPage()const noexcept{return importPage_;}
    bool importRequired()const noexcept{return importRequired_;}
    bool importEnabled()const noexcept{return bool(importer_);}
    const PlayerImportSource& importSource()const noexcept{return importSource_;}
    const PlayerImportOptions& importOptions()const noexcept{return importOptions_;}
    const std::wstring& importMessage()const noexcept{return importMessage_;}
private:
    NativeLaunchPaths paths_;
    NativeDisplayPreferences saved_,pending_;
    std::optional<bool> fullscreenOverride_,interpolationOverride_;
    unsigned edited_{};
    bool ready_{},defaultsApproved_{};
    std::wstring source_,error_;
    std::unique_ptr<PlayerDataImport> importer_;
    PlayerImportSource importSource_;
    PlayerImportOptions importOptions_;
    LauncherImportPage importPage_{LauncherImportPage::None};
    bool importRequired_{};
    std::wstring importMessage_;
    std::future<std::pair<std::wstring,bool>> importWork_;
};
} // namespace scr::runtime

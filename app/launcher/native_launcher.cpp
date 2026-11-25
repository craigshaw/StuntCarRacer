#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include "launcher/native_launcher.h"
#include "assets/local_asset_pack.h"
#include "platform/windows/shared_hall_storage.h"
#include "persistence/enhanced_hall_store.h"
#include "persistence/native_career_store.h"
#include "persistence/windows_file_path.h"
#include <stdexcept>

namespace scr::runtime {
std::filesystem::path nativeExecutablePath(){
    std::wstring path(32768,L'\0');const auto count=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!count||count>=path.size())throw std::runtime_error("Cannot locate this game executable");path.resize(count);return path;
}
NativeLaunchPaths defaultNativeLaunchPaths(){const auto layout=portablePlayerDataLayout(nativeExecutablePath());return {layout.pack,layout.settings,{},layout.root};}
PlayerImportSource legacyPlayerImportSource(){
    PWSTR folder{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&folder)))
        throw std::runtime_error("Cannot locate your Windows application-data folder.");
    const auto root=std::filesystem::path(folder)/L"Stunt Car Racer";CoTaskMemFree(folder);
    const auto original=sharedHallPaths(nativeExecutablePath().parent_path()).root;return playerImportSource(root,original,enhancedHallRoot(original));
}
std::wstring launcherErrorText(const std::exception& error){
    const auto count=MultiByteToWideChar(CP_UTF8,0,error.what(),-1,nullptr,0);
    if(count<=1)return L"The operation failed. Check the selected file and data folder, then retry.";
    std::wstring text(std::size_t(count),L'\0');MultiByteToWideChar(CP_UTF8,0,error.what(),-1,text.data(),count);text.pop_back();return text;
}
NativeLauncher::NativeLauncher(NativeLaunchPaths paths,std::optional<bool> fullscreen,std::optional<bool> interpolate):
    paths_(std::move(paths)),saved_(loadDisplayPreferences(paths_.settings)),pending_(saved_),
    fullscreenOverride_(fullscreen),interpolationOverride_(interpolate){
    if(fullscreen)pending_.fullscreen=*fullscreen;if(interpolate)pending_.interpolate=*interpolate;
    refreshAssets();
}
void NativeLauncher::refreshAssets(){
    ready_=false;source_.clear();error_.clear();
    std::error_code error;
    if(!std::filesystem::exists(paths_.pack,error)&&!error)return;
    try{(void)loadLocalAssetPack(paths_.pack);ready_=true;source_=localAssetSourceName(paths_.pack);}
    catch(const std::exception& failure){error_=L"Imported game data is missing or damaged. Choose your supported ADF to repair it.\n"+launcherErrorText(failure);}
}
bool NativeLauncher::importDisk(const std::filesystem::path& disk){
    try{importLocalAssetPack(disk,paths_.pack);refreshAssets();return ready_;}
    catch(const std::exception& failure){error_=launcherErrorText(failure);return false;}
}
void NativeLauncher::change(unsigned row,int direction){
    if(row>8)return;
    edited_|=1u<<row;
    changeDisplayPreference(pending_,row,direction);
}
void NativeLauncher::resetPreferences(){pending_={};pending_.navigation=NativeMenuNavigation::Original;pending_.timing=TimingCategory::Original;edited_=511;defaultsApproved_=true;}
bool NativeLauncher::saveForPlay(){
    if(!ready_||importPage_!=LauncherImportPage::None||importRequired_)return false;
    auto persisted=pending_;
    if(fullscreenOverride_&&!(edited_&1))persisted.fullscreen=saved_.fullscreen;
    if(interpolationOverride_&&!(edited_&4))persisted.interpolate=saved_.interpolate;
    try{
        if(std::filesystem::exists(paths_.settings)&&!readDisplayPreferences(paths_.settings)){
            if(!defaultsApproved_)throw std::runtime_error("Settings are damaged or unsupported. Choose Restore original defaults to retain that file and save new settings.");
            if(FolderTransactions::directoryLink(paths_.settings))throw std::runtime_error("Settings file links cannot be replaced");
            const auto preserved=paths_.settings.wstring()+L".unsupported-"+std::filesystem::path(NativeCareerStore::newId()).wstring();
            if(!MoveFileExW(windowsFilePath(paths_.settings).c_str(),windowsFilePath(preserved).c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve the previous settings file");
        }
        if(!saveDisplayPreferences(paths_.settings,persisted))throw std::runtime_error("Your settings could not be saved. Check that your data folder is writable, then press Play to retry.");
        saved_=persisted;error_.clear();return true;
    }catch(const std::exception& failure){error_=launcherErrorText(failure);return false;}
}
void NativeLauncher::failedStart(const std::exception& failure){error_=L"The game could not start. Your game data and selected settings are retained.\n"+launcherErrorText(failure);}
} // namespace scr::runtime

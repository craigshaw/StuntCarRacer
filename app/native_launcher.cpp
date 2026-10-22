#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include "native_launcher.h"
#include "local_asset_pack.h"
#include <stdexcept>

namespace scr::viewer {
NativeLaunchPaths defaultNativeLaunchPaths(){
    PWSTR folder{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&folder)))
        throw std::runtime_error("Cannot locate your Windows application-data folder.");
    const auto root=std::filesystem::path(folder)/L"Stunt Car Racer";CoTaskMemFree(folder);
    return {root/L"assets/stunt-car-racer.scr-assets",root/L"settings/display.ini",root/L"saves/stunt-car-racer.scr-save",{}};
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
    if(row>3)return;
    edited_|=1u<<row;
    if(row==0)pending_.fullscreen=!pending_.fullscreen;
    else if(row==1)pending_.scaling=NativeScaling((unsigned(pending_.scaling)+(direction<0?2:1))%3);
    else if(row==2)pending_.interpolate=!pending_.interpolate;
    else pending_.controls=pending_.controls==DriveControlPreset::Original?DriveControlPreset::Modern:DriveControlPreset::Original;
}
void NativeLauncher::resetPreferences(){pending_={};edited_=15;}
bool NativeLauncher::saveForPlay(){
    if(!ready_)return false;
    auto persisted=pending_;
    if(fullscreenOverride_&&!(edited_&1))persisted.fullscreen=saved_.fullscreen;
    if(interpolationOverride_&&!(edited_&4))persisted.interpolate=saved_.interpolate;
    try{
        if(!saveDisplayPreferences(paths_.settings,persisted))throw std::runtime_error("Your settings could not be saved. Check that your data folder is writable, then press Play to retry.");
        saved_=persisted;error_.clear();return true;
    }catch(const std::exception& failure){error_=launcherErrorText(failure);return false;}
}
void NativeLauncher::failedStart(const std::exception& failure){error_=L"The game could not start. Your game data and selected settings are retained.\n"+launcherErrorText(failure);}
} // namespace scr::viewer

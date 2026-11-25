#include "launcher/native_launcher.h"
#include "persistence/enhanced_hall_store.h"
#include <algorithm>
#include <chrono>

namespace scr::runtime {
namespace fs=std::filesystem;
void NativeLauncher::configureDataImport(bool discoverLegacy,std::optional<PlayerImportSource> explicitSource){
    importer_=std::make_unique<PlayerDataImport>(paths_.dataRoot);
    if(explicitSource){importSource_=*explicitSource;importRequired_=!importer_->decided();openDataImport();return;}
    if(importer_->decided())return;
    bool flat=false;const auto careers=paths_.dataRoot/L"careers";
    if(fs::is_directory(careers))for(const auto& e:fs::directory_iterator(careers))flat|=e.is_regular_file()&&e.path().filename().wstring().find(L".scr-career")!=std::wstring::npos;
    if(flat){importSource_=playerImportSource(paths_.dataRoot);importRequired_=true;openDataImport();return;}
    bool populated=false;for(const auto* name:{L"assets",L"settings.ini",L"careers",L"multiplayer",L"hall-of-fame"})populated|=fs::exists(paths_.dataRoot/name);
    if(!populated&&discoverLegacy)try{auto candidate=legacyPlayerImportSource();if(hasPlayerImportSource(candidate)){importSource_=std::move(candidate);importRequired_=true;openDataImport();return;}}
    catch(const std::exception& error){importRequired_=true;openDataImport();importMessage_=L"Choose the old data folder or start fresh.\n"+launcherErrorText(error);return;}
    importer_->startFresh();
}
void NativeLauncher::openDataImport(){
    if(!importer_||importWork_.valid())return;importPage_=LauncherImportPage::Review;importOptions_={};importMessage_.clear();
}
void NativeLauncher::importFolder(const fs::path& path,bool hall){
    if(importPage_!=LauncherImportPage::Review)return;
    if(hall){if(importSource_.root.empty())importSource_=playerImportSource(paths_.dataRoot);
        importSource_.originalHall=path;importSource_.enhancedHall=path.filename()==L"original"&&path.parent_path().filename()==L"hall-of-fame"?path.parent_path()/L"enhanced":enhancedHallRoot(path);}
    else importSource_=playerImportSource(path);importMessage_.clear();
}
void NativeLauncher::changeImportOption(unsigned option){
    if(importPage_!=LauncherImportPage::Review)return;
    if(option==0)importOptions_.replaceSettings=!importOptions_.replaceSettings;
    if(option==1)importOptions_.replaceOriginalHall=!importOptions_.replaceOriginalHall;
    if(option==2)importOptions_.replaceEnhancedHall=!importOptions_.replaceEnhancedHall;
}
void NativeLauncher::applyDataImport(){
    if(!importer_||importWork_.valid()||importSource_.root.empty())return;
    importPage_=LauncherImportPage::Working;importMessage_=L"Copying and validating player data. Your old files stay unchanged.";
    const auto source=importSource_;const auto options=importOptions_;
    importWork_=std::async(std::launch::async,[this,source,options]{
        std::wstring message;
        try{const auto r=importer_->import(source,options);message=L"Imported "+std::to_wstring(r.careers)+L" careers and "+std::to_wstring(r.multiplayer)+L" multiplayer games.\n";
            if(r.conflicts)message+=L"Copied identities need Replace / Keep Both / Cancel in Browse Saved Games.\n";
            message+=std::wstring(L"Original Hall: ")+(r.originalHall?L"copied":L"kept")+L". Enhanced Hall: "+(r.enhancedHall?L"copied":L"kept")+L".\n";
            if(!r.warnings.empty()){message+=L"Some files were kept or are unavailable. Details are retained in migration-recovery.\n";for(unsigned n=0;n<std::min<std::size_t>(3,r.warnings.size());++n)message+=launcherErrorText(std::runtime_error(r.warnings[n]))+L"\n";}
        }catch(const std::exception& error){message=L"Import could not complete. Your original files are retained.\n"+launcherErrorText(error);}
        bool ready=false;try{PlayerDataImport recovered(paths_.dataRoot);ready=recovered.decided();}
        catch(const std::exception& error){message+=L"\nRecovery must finish before Play.\n"+launcherErrorText(error);}
        return std::make_pair(message,ready);
    });
}
void NativeLauncher::freshData(){
    if(!importer_||importWork_.valid())return;
    try{importer_->startFresh();importRequired_=false;importPage_=LauncherImportPage::None;refreshAssets();}
    catch(const std::exception& error){importMessage_=launcherErrorText(error);}
}
void NativeLauncher::closeDataImport(){
    if(importWork_.valid())return;
    if(importPage_==LauncherImportPage::Report&&importRequired_){importPage_=LauncherImportPage::Review;return;}
    if(!importRequired_)importPage_=LauncherImportPage::None;
}
bool NativeLauncher::pollDataImport(){
    if(!importWork_.valid()||importWork_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
    const auto result=importWork_.get();importMessage_=result.first;importPage_=LauncherImportPage::Report;importRequired_=!result.second;
    if(importRequired_){ready_=false;source_.clear();return true;}
    saved_=loadDisplayPreferences(paths_.settings);pending_=saved_;if(fullscreenOverride_)pending_.fullscreen=*fullscreenOverride_;if(interpolationOverride_)pending_.interpolate=*interpolationOverride_;refreshAssets();return true;
}
} // namespace scr::runtime

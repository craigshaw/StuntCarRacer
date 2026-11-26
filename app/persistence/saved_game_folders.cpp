#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "persistence/saved_game_folders.h"
#include "persistence/career_file_io.h"
#include "persistence/player_preferences.h"
#include "persistence/windows_file_path.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
namespace fs=std::filesystem;
namespace {
bool usable(SavedGameFileState state){return state==SavedGameFileState::Valid||state==SavedGameFileState::Recovered;}
void order(std::vector<SavedGameEntry>& entries){
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.lastPlayed!=b.lastPlayed?a.lastPlayed>b.lastPlayed:a.folder.native()<b.folder.native();});
}
std::string label(const std::string& name){
    std::string result;for(unsigned char c:name)result+=c>=32&&c<127&&std::string("<>:\"/\\|?*").find(char(c))==std::string::npos?char(c):'_';
    while(!result.empty()&&(result.back()=='.'||result.back()==' '))result.pop_back();return result.empty()?"Saved Game":result;
}
}
SavedGameFolders::SavedGameFolders(fs::path root,SavedGameFolderFormat format):root_(std::move(root)),format_(std::move(format)){
    if((format_.category!=L"careers"&&format_.category!=L"multiplayer")||format_.head.has_parent_path())throw std::invalid_argument("Invalid saved-game folder format");
    refresh();
}
void SavedGameFolders::check(const fs::path& folder)const{
    if(folder.parent_path()!=root_/format_.category||!validSavedGameFolder(folder.filename().u8string()))throw std::invalid_argument("Invalid saved-game folder");
    if(windowsPathLink(folder.parent_path())||windowsPathLink(folder))throw std::runtime_error("Saved-game folder links are not followed");
}
void SavedGameFolders::refresh(){
    std::vector<SavedGameEntry> entries;const auto category=root_/format_.category;
    if(windowsPathLink(category))throw std::runtime_error("Saved-game folder links are not followed");
    if(fs::is_directory(windowsFilePath(category)))for(const auto& child:fs::directory_iterator(windowsFilePath(category))){
        const auto folder=category/child.path().filename();
        if(windowsPathLink(folder)||!child.is_directory())continue;
        const auto head=folder/format_.head;bool found=false,linked=false;
        for(unsigned n=0;n<3;++n){const auto file=career_files::generationPath(head,n);linked|=windowsPathLink(file);found|=fs::exists(windowsFilePath(file));}
        if(!found||linked)continue;
        SavedGameEntry info;info.name="Unavailable";const auto saved=format_.read(head);info.state=saved.state;
        if(usable(saved.state)){info=format_.describe(saved.data);info.state=saved.state;}
        else for(unsigned n=0;n<3;++n){const auto previous=format_.readGeneration(career_files::generationPath(head,n));
            if(previous.state==SavedGameFileState::Valid){const auto state=info.state;info=format_.describe(previous.data);info.state=state;break;}}
        info.folder=folder;entries.push_back(std::move(info));
    }
    order(entries);std::lock_guard<std::mutex> lock(mutex_);entries_=std::move(entries);
}
std::vector<SavedGameEntry> SavedGameFolders::list()const{std::lock_guard<std::mutex> lock(mutex_);return entries_;}
fs::path SavedGameFolders::directory(const std::string& key)const{
    if(!validSavedGameFolder(key))throw std::invalid_argument("Invalid saved-game selection");
    std::lock_guard<std::mutex> lock(mutex_);
    for(const auto& entry:entries_)if(entry.key()==key){check(entry.folder);return entry.folder;}
    for(const auto& draft:drafts_)if(draft.second.filename().u8string()==key){check(draft.second);return draft.second;}
    throw std::out_of_range("Saved-game folder not found");
}
fs::path SavedGameFolders::reserve(const SavedGameEntry& info){
    if(!validPlayerIdentity(info.id))throw std::invalid_argument("Invalid saved-game identity");
    if(!info.folder.empty()){check(info.folder);return info.folder;}
    std::lock_guard<std::mutex> lock(mutex_);
    if(const auto held=drafts_.find(info.id);held!=drafts_.end()){check(held->second);return held->second;}
    const auto base=label(info.name)+"-"+info.id.substr(0,8);
    for(unsigned n=0;n<1000;++n){const auto path=root_/format_.category/fs::u8path(base+(n?"-"+std::to_string(n):""));check(path);
        if(!fs::exists(windowsFilePath(path))){drafts_[info.id]=path;return path;}}
    throw std::runtime_error("Cannot choose a saved-game folder name");
}
void SavedGameFolders::update(SavedGameEntry info,const fs::path& folder){
    check(folder);info.folder=folder;info.state=SavedGameFileState::Valid;std::lock_guard<std::mutex> lock(mutex_);
    const auto old=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.folder==folder;});
    if(old==entries_.end())entries_.push_back(std::move(info));else *old=std::move(info);order(entries_);
}
void SavedGameFolders::cancelDraft(const SavedGameEntry& info){
    {std::lock_guard<std::mutex> lock(mutex_);const auto held=drafts_.find(info.id);
        if(held==drafts_.end()||held->second!=info.folder)throw std::logic_error("Only a new saved game can be cancelled");}
    check(info.folder);const auto head=info.folder/format_.head;
    for(unsigned n=0;n<3;++n){const auto file=career_files::generationPath(head,n);if(windowsPathLink(file))throw std::runtime_error("Saved-game file links are not followed");
        const auto saved=format_.readGeneration(file);if(saved.state!=SavedGameFileState::Missing&&(!usable(saved.state)||format_.describe(saved.data).id!=info.id))throw std::runtime_error("New saved game changed; files preserved");}
    for(unsigned n=0;n<3;++n){std::error_code error;const auto file=windowsFilePath(career_files::generationPath(head,n));if(fs::exists(file,error))fs::remove(file);}
    // Never recursively remove a folder: unrelated files belong to its owner.
    std::error_code error;if(fs::is_empty(windowsFilePath(info.folder),error)&&!error)fs::remove(windowsFilePath(info.folder));
    {std::lock_guard<std::mutex> lock(mutex_);drafts_.erase(info.id);}refresh();
}
} // namespace scr::runtime

#include "persistence/saved_game_folders.h"
#include "persistence/native_career_store.h"
#include "persistence/career_file_io.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <cwctype>

namespace scr::runtime {
namespace fs=std::filesystem;
namespace {bool usable(SavedGameFileState state){return state==SavedGameFileState::Valid||state==SavedGameFileState::Recovered;}}
SavedGameFolders::SavedGameFolders(fs::path root,SavedGameFolderFormat format,FolderTransactionObserver observer):
    root_(std::move(root)),format_(std::move(format)),transactions_(root_,std::move(observer)){
    if((format_.category!=L"careers"&&format_.category!=L"multiplayer")||format_.head.has_parent_path())throw std::invalid_argument("Invalid saved-game folder format");
    transactions_.recover();refresh();
}
std::wstring SavedGameFolders::folded(std::wstring name){std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(towlower(c));});return name;}
std::string SavedGameFolders::label(const std::string& name){
    std::string result;for(unsigned char c:name)result+=c>=32&&c<127&&std::string("<>:\"/\\|?*").find(char(c))==std::string::npos?char(c):'_';
    while(!result.empty()&&(result.back()=='.'||result.back()==' '))result.pop_back();return result.empty()?"Saved Game":result;
}
void SavedGameFolders::conflicts(std::vector<SavedGameEntry>& entries){
    std::map<std::string,unsigned> counts;for(const auto& entry:entries)if(!entry.id.empty())++counts[entry.id];
    for(auto& entry:entries)if(!entry.id.empty()&&counts[entry.id]>1)entry.state=CareerFileState::Conflict;
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.lastPlayed!=b.lastPlayed?a.lastPlayed>b.lastPlayed:a.folder.native()<b.folder.native();});
}
void SavedGameFolders::refresh(){
    std::vector<SavedGameEntry> entries;std::set<std::wstring> names;const auto careers=root_/format_.category;
    if(FolderTransactions::directoryLink(careers))throw std::runtime_error("Saved-game directory links are not followed");
    if(fs::exists(careers)){
        for(const auto& child:fs::directory_iterator(careers)){
            names.insert(folded(child.path().filename().wstring()));
            SavedGameEntry info;info.name="Unavailable";info.folder=child.path();info.state=CareerFileState::Corrupt;
            if(FolderTransactions::directoryLink(child.path())){entries.push_back(std::move(info));continue;}
            if(!child.is_directory())continue;
            const auto head=child.path()/format_.head;bool found=false,linked=false;
            for(unsigned n=0;n<3;++n){const auto file=career_files::generationPath(head,n);const bool link=FolderTransactions::directoryLink(file);linked|=link;found|=link||fs::exists(file);}
            if(!found)continue;
            if(!linked){const auto saved=format_.read(head);info.state=saved.state;
                if(usable(saved.state)){info=format_.describe(saved.data);info.state=saved.state;}
                else for(unsigned n=0;n<3;++n){const auto previous=format_.readGeneration(career_files::generationPath(head,n));
                    if(previous.state==CareerFileState::Valid){const auto state=info.state;info=format_.describe(previous.data);info.state=state;break;}}
                info.folder=child.path();
            }
            entries.push_back(std::move(info));
        }
    }
    conflicts(entries);std::lock_guard<std::mutex> lock(mutex_);for(const auto& held:reserved_)names.insert(folded(held.second.filename().wstring()));entries_=std::move(entries);names_=std::move(names);
}
std::vector<SavedGameEntry> SavedGameFolders::list()const{std::lock_guard<std::mutex> lock(mutex_);return entries_;}
fs::path SavedGameFolders::directory(const std::string& id)const{
    if(!validPlayerIdentity(id))throw std::invalid_argument("Invalid saved-game identity");
    std::lock_guard<std::mutex> lock(mutex_);const SavedGameEntry* found=nullptr;
    for(const auto& entry:entries_)if(entry.id==id){if(found)throw std::runtime_error("Resolve the copied saved-game conflict first");found=&entry;}
    if(found)return found->folder;
    if(const auto held=reserved_.find(id);held!=reserved_.end())return held->second;
    throw std::out_of_range("Saved-game folder not found");
}
fs::path SavedGameFolders::reserve(const SavedGameEntry& info){
    try{return directory(info.id);}catch(const std::out_of_range&){}
    std::lock_guard<std::mutex> lock(mutex_);const auto base=label(info.name)+"-"+info.id.substr(0,8);
    for(unsigned n=0;n<1000;++n){const auto name=base+(n?"-"+std::to_string(n):"");const auto wide=fs::u8path(name).wstring();
        if(names_.insert(folded(wide)).second){auto path=root_/format_.category/wide;reserved_[info.id]=path;return path;}}
    throw std::runtime_error("Cannot choose a unique saved-game folder name");
}
bool SavedGameFolders::known(const fs::path& folder)const{
    std::lock_guard<std::mutex> lock(mutex_);return std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.folder==folder;});
}
void SavedGameFolders::update(SavedGameEntry info,const fs::path& folder){
    info.folder=folder;info.state=CareerFileState::Valid;std::lock_guard<std::mutex> lock(mutex_);
    const auto old=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.folder==folder;});
    if(old==entries_.end())entries_.push_back(std::move(info));else *old=std::move(info);conflicts(entries_);
}
void SavedGameFolders::recover(){transactions_.recover();refresh();}
void SavedGameFolders::create(const fs::path& folder,const std::function<void(const fs::path&)>& build){transactions_.publish(folder,{},build);}
std::string SavedGameFolders::resolve(const SavedGameEntry& incoming,SavedGameCopyChoice choice,const std::string& newId){
    transactions_.recover();refresh();const auto entries=list();std::vector<SavedGameEntry> copies;
    for(const auto& entry:entries)if(entry.id==incoming.id)copies.push_back(entry);
    const auto source=std::find_if(copies.begin(),copies.end(),[&](const auto& entry){return entry.folder==incoming.folder;});
    if(source==copies.end()||copies.size()<2)throw std::runtime_error("The copied saved-game conflict has changed");
    const auto saved=format_.read(source->folder/format_.head);
    if(!usable(saved.state))throw std::runtime_error("The selected saved-game copy is damaged or unsupported");
    auto selected=format_.describe(saved.data);const auto sourceFolder=source->folder;
    std::vector<fs::path> retire;fs::path target;
    if(choice==SavedGameCopyChoice::KeepBoth){selected.id=newId;target=reserve(selected);retire.push_back(sourceFolder);}
    else{target=sourceFolder;for(const auto& copy:copies)retire.push_back(copy.folder);}
    transactions_.publish(target,retire,[&](const auto& stage){
        FolderTransactions::copy(sourceFolder,stage);
        if(choice==SavedGameCopyChoice::KeepBoth)format_.rekey(stage,incoming.id,newId);
        else{
            const auto history=stage/L"recovery"/L"replaced"/NativeCareerStore::newId();
            for(const auto& copy:copies)if(copy.folder!=sourceFolder)FolderTransactions::copy(copy.folder,history/copy.folder.filename());
        }
        const auto result=format_.read(stage/format_.head);
        if(!usable(result.state)||format_.describe(result.data).id!=selected.id)throw std::runtime_error("Copied saved game failed validation");
    });refresh();return selected.id;
}
void SavedGameFolders::erase(const SavedGameEntry& entry){
    recover();const auto entries=list();
    if(entry.folder.parent_path()==root_/format_.category&&!std::filesystem::exists(entry.folder)){forget(entry.id);return;}
    if(std::none_of(entries.begin(),entries.end(),[&](const auto& current){return current.folder==entry.folder;}))throw std::runtime_error("Saved-game folder is no longer available");
    transactions_.erase(entry.folder);refresh();std::lock_guard<std::mutex> lock(mutex_);if(!entry.id.empty())reserved_.erase(entry.id);
}
void SavedGameFolders::forget(const std::string& id){std::lock_guard<std::mutex> lock(mutex_);reserved_.erase(id);}
} // namespace scr::runtime

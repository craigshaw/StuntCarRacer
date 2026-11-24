#include "persistence/career_folders.h"
#include "persistence/career_file_io.h"
#include "persistence/ghost_lap_codec.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace scr::runtime {
namespace fs=std::filesystem;
namespace {
bool usable(CareerFileState state){return state==CareerFileState::Valid||state==CareerFileState::Recovered;}
bool member(const std::wstring& name,const std::wstring& base){
    if(name==base||name==base+L".bak1"||name==base+L".bak2")return true;
    return name.rfind(base+L".damaged-",0)==0||name.rfind(base+L".bak1.damaged-",0)==0||name.rfind(base+L".bak2.damaged-",0)==0;
}
unsigned kind(const fs::path& path){
    const auto name=path.filename().wstring();if(member(name,L"career.scr-career"))return 1;
    for(const auto& ext:{std::wstring(L".scr-ghost"),std::wstring(L".scr-ebest")}){
        const auto at=name.find(ext);if(at!=std::wstring::npos&&member(name,name.substr(0,at)+ext))return ext==L".scr-ghost"?4u:5u;
    }return 0;
}
}
std::wstring CareerFolders::folded(std::wstring name){std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(towlower(c));});return name;}
std::string CareerFolders::label(const std::string& name){
    std::string result;for(unsigned char c:name)result+=c>=32&&c<127&&std::string("<>:\"/\\|?*").find(char(c))==std::string::npos?char(c):'_';
    while(!result.empty()&&(result.back()=='.'||result.back()==' '))result.pop_back();return result.empty()?"Career":result;
}
CareerFolders::CareerFolders(fs::path root,NativeCareerStore::Validator validator,FolderTransactionObserver observer):
    root_(std::move(root)),validator_(std::move(validator)),transactions_(root_,std::move(observer)){transactions_.recover();refresh();}
void CareerFolders::conflicts(std::vector<NativeCareerInfo>& entries){
    std::map<std::string,unsigned> counts;for(const auto& entry:entries)if(!entry.id.empty())++counts[entry.id];
    for(auto& entry:entries)if(!entry.id.empty()&&counts[entry.id]>1)entry.state=CareerFileState::Conflict;
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.lastPlayed!=b.lastPlayed?a.lastPlayed>b.lastPlayed:a.folder.native()<b.folder.native();});
}
void CareerFolders::refresh(){
    std::vector<NativeCareerInfo> entries;std::set<std::wstring> names;const auto careers=root_/L"careers";
    if(FolderTransactions::directoryLink(careers))throw std::runtime_error("Career directory links are not followed");
    if(fs::exists(careers)){
        for(const auto& child:fs::directory_iterator(careers)){
            names.insert(folded(child.path().filename().wstring()));
            NativeCareerInfo info;info.name="Unavailable";info.folder=child.path();info.state=CareerFileState::Corrupt;
            if(FolderTransactions::directoryLink(child.path())){entries.push_back(std::move(info));continue;}
            if(!child.is_directory())continue;
            const auto head=child.path()/L"career.scr-career";bool found=false,linked=false;
            for(unsigned n=0;n<3;++n){const auto file=career_files::generationPath(head,n);const bool link=FolderTransactions::directoryLink(file);linked|=link;found|=link||fs::exists(file);}
            if(!found)continue;
            if(!linked){const auto saved=NativeCareerStore::readFile(head,1,validator_);info.state=saved.state;
                if(usable(saved.state)){info=decodeCareerInfo(saved.data);info.state=saved.state;}
                else for(unsigned n=0;n<3;++n){const auto previous=career_files::readGeneration(career_files::generationPath(head,n),1);
                    if(previous.state==CareerFileState::Valid){const auto state=info.state;info=decodeCareerInfo(previous.data);info.state=state;break;}}
                info.folder=child.path();
            }
            entries.push_back(std::move(info));
        }
    }
    conflicts(entries);std::lock_guard<std::mutex> lock(mutex_);entries_=std::move(entries);names_=std::move(names);
}
std::vector<NativeCareerInfo> CareerFolders::list()const{std::lock_guard<std::mutex> lock(mutex_);return entries_;}
fs::path CareerFolders::directory(const std::string& id)const{
    if(!validPlayerIdentity(id))throw std::invalid_argument("Invalid career identity");
    std::lock_guard<std::mutex> lock(mutex_);const NativeCareerInfo* found=nullptr;
    for(const auto& entry:entries_)if(entry.id==id){if(found)throw std::runtime_error("Resolve the copied career conflict first");found=&entry;}
    if(found)return found->folder;
    if(const auto held=reserved_.find(id);held!=reserved_.end())return held->second;
    throw std::out_of_range("Career folder not found");
}
fs::path CareerFolders::reserve(const NativeCareerInfo& info){
    try{return directory(info.id);}catch(const std::out_of_range&){}
    std::lock_guard<std::mutex> lock(mutex_);const auto base=label(info.name)+"-"+info.id.substr(0,8);
    for(unsigned n=0;n<1000;++n){const auto name=base+(n?"-"+std::to_string(n):"");const auto wide=fs::u8path(name).wstring();
        if(names_.insert(folded(wide)).second){auto path=root_/L"careers"/wide;reserved_[info.id]=path;return path;}}
    throw std::runtime_error("Cannot choose a unique career folder name");
}
bool CareerFolders::known(const fs::path& folder)const{
    std::lock_guard<std::mutex> lock(mutex_);return std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.folder==folder;});
}
void CareerFolders::update(NativeCareerInfo info,const fs::path& folder){
    info.folder=folder;info.state=CareerFileState::Valid;std::lock_guard<std::mutex> lock(mutex_);
    const auto old=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.folder==folder;});
    if(old==entries_.end())entries_.push_back(std::move(info));else *old=std::move(info);conflicts(entries_);
}
void CareerFolders::recover(){if(transactions_.recover())refresh();}
void CareerFolders::create(const fs::path& folder,const std::function<void(const fs::path&)>& build){transactions_.publish(folder,{},build);}
void CareerFolders::rekey(const fs::path& folder,const std::string& oldId,const std::string& newId){
    std::vector<fs::path> files;for(const auto& entry:fs::recursive_directory_iterator(folder))if(entry.is_regular_file())files.push_back(entry.path());
    for(const auto& path:files){
        const auto fileKind=kind(path);
        if(!fileKind){
            const auto name=path.filename().wstring();
            if(name.find(L".scr-")!=std::wstring::npos&&name.find(L".tmp")==std::wstring::npos)
                throw std::runtime_error("Cannot rekey an unknown saved-game file format");
            continue;
        }
        auto saved=career_files::readGeneration(path,fileKind);
        if(saved.state==CareerFileState::Unsupported)throw std::runtime_error("Cannot rekey an unsupported saved-game generation");
        if(saved.state!=CareerFileState::Valid)continue; // Preserve unreadable evidence verbatim.
        if(fileKind==1){CareerBytes checkpoint;auto info=decodeCareerInfo(saved.data,&checkpoint);
            if(info.id!=oldId)throw std::runtime_error("Recovery career ownership differs");info.id=newId;saved.data=encodeCareerInfo(info,checkpoint);}
        else if(fileKind==4){auto lap=decodeGhostLap(saved.data);if(lap.context.key.career!=oldId)throw std::runtime_error("Ghost ownership differs");lap.context.key.career=newId;saved.data=encodeGhostLap(lap);}
        else{auto best=decodeEnhancedBest(saved.data);if(best.key.career!=oldId)throw std::runtime_error("Enhanced PB ownership differs");best.key.career=newId;saved.data=encodeEnhancedBest(best);}
        std::string error;if(!career_files::atomicWrite(path,career_files::envelope(fileKind,saved.generation,saved.data),error))throw std::runtime_error(error);
    }
}
std::string CareerFolders::resolve(const NativeCareerInfo& incoming,CareerCopyChoice choice,const std::string& newId){
    transactions_.recover();refresh();const auto entries=list();std::vector<NativeCareerInfo> copies;
    for(const auto& entry:entries)if(entry.id==incoming.id)copies.push_back(entry);
    const auto source=std::find_if(copies.begin(),copies.end(),[&](const auto& entry){return entry.folder==incoming.folder;});
    if(source==copies.end()||copies.size()<2)throw std::runtime_error("The copied career conflict has changed");
    const auto saved=NativeCareerStore::readFile(source->folder/L"career.scr-career",1,validator_);
    if(!usable(saved.state))throw std::runtime_error("The selected career copy is damaged or unsupported");
    auto selected=decodeCareerInfo(saved.data);const auto sourceFolder=source->folder;
    std::vector<fs::path> retire;fs::path target;
    if(choice==CareerCopyChoice::KeepBoth){selected.id=newId;target=reserve(selected);retire.push_back(sourceFolder);}
    else{target=sourceFolder;for(const auto& copy:copies)retire.push_back(copy.folder);}
    transactions_.publish(target,retire,[&](const auto& stage){
        FolderTransactions::copy(sourceFolder,stage);
        if(choice==CareerCopyChoice::KeepBoth)rekey(stage,incoming.id,newId);
        else{
            const auto history=stage/L"recovery"/L"replaced"/NativeCareerStore::newId();
            for(const auto& copy:copies)if(copy.folder!=sourceFolder)FolderTransactions::copy(copy.folder,history/copy.folder.filename());
        }
        const auto result=NativeCareerStore::readFile(stage/L"career.scr-career",1,validator_);
        if(!usable(result.state)||decodeCareerInfo(result.data).id!=selected.id)throw std::runtime_error("Copied career failed validation");
    });refresh();return selected.id;
}
void CareerFolders::erase(const NativeCareerInfo& entry){
    transactions_.recover();const auto entries=list();
    if(std::none_of(entries.begin(),entries.end(),[&](const auto& current){return current.folder==entry.folder;}))throw std::runtime_error("Career folder is no longer available");
    transactions_.erase(entry.folder);refresh();std::lock_guard<std::mutex> lock(mutex_);if(!entry.id.empty())reserved_.erase(entry.id);
}
void CareerFolders::forget(const std::string& id){std::lock_guard<std::mutex> lock(mutex_);reserved_.erase(id);}
} // namespace scr::runtime

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
CareerFolders::CareerFolders(fs::path root,NativeCareerStore::Validator validator,FolderTransactionObserver observer):
    SavedGameFolders(std::move(root),{L"careers",L"career.scr-career",
        [validator](const auto& path){return NativeCareerStore::readFile(path,1,validator);},
        [](const auto& path){return career_files::readGeneration(path,1);},
        [](const auto& data){return decodeCareerInfo(data);},
        [this](const auto& folder,const auto& oldId,const auto& newId){rekey(folder,oldId,newId);}},std::move(observer)){}
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
} // namespace scr::runtime

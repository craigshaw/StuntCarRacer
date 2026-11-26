#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/native_multiplayer_store.h"
#include "persistence/career_file_io.h"
#include "persistence/windows_file_path.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>

namespace scr::runtime {
namespace fs=std::filesystem;
namespace {
bool usable(SavedGameFileState state){return state==SavedGameFileState::Valid||state==SavedGameFileState::Recovered;}

}
NativeMultiplayerStore::NativeMultiplayerStore(NativeCareerStore& worker,const NativeMemory& baseline,fs::path settings):
    worker_(worker),baseline_(std::make_unique<NativeMemory>(baseline)),settings_(settings.empty()?worker.root()/L"settings.ini":std::move(settings)),folders_(worker.root(),format()){}
NativeMultiplayerStore::~NativeMultiplayerStore(){worker_.flush();worker_.forgetWrites(worker_.root()/L"multiplayer");worker_.forgetWrites(preferenceKey());}
SavedGameFolderFormat NativeMultiplayerStore::format(){return {L"multiplayer",L"game.scr-multiplayer",
    [this](const auto& path){return recoveries(path);},[this](const auto& path){return generation(path,false);},
    [this](const auto& data){return describe(data);}};}
SavedGameRead NativeMultiplayerStore::generation(const fs::path& path,bool validate)const{
    if(windowsPathLink(path))return {SavedGameFileState::Unsupported};
    std::error_code error;if(!fs::exists(path,error))return {error?SavedGameFileState::Corrupt:SavedGameFileState::Missing};
    const auto size=fs::file_size(path,error);if(error||size<20||size>nativeMultiplayerSaveMaximumBytes)return {SavedGameFileState::Corrupt};
    std::ifstream file(windowsFilePath(path),std::ios::binary);MultiplayerBytes bytes(std::size_t(size),0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));if(!file)return {SavedGameFileState::Corrupt};
    try{const auto saved=validate?decodeNativeMultiplayerSave(*baseline_,bytes):decodeNativeMultiplayerEnvelope(bytes);return {SavedGameFileState::Valid,std::move(bytes),saved.generation};}
    catch(const UnsupportedNativeMultiplayerCheckpoint&){return {SavedGameFileState::Unsupported};}catch(const std::exception&){return {SavedGameFileState::Corrupt};}
}
SavedGameRead NativeMultiplayerStore::recoveries(const fs::path& path)const{
    auto best=generation(path);if(best.state==SavedGameFileState::Valid||best.state==SavedGameFileState::Unsupported)return best;
    for(unsigned n=1;n<3;++n){auto copy=generation(career_files::generationPath(path,n));if(copy.state==SavedGameFileState::Unsupported)return copy;
        if(copy.state==SavedGameFileState::Corrupt&&best.state==SavedGameFileState::Missing)best.state=SavedGameFileState::Corrupt;
        if(copy.state==SavedGameFileState::Valid&&(!usable(best.state)||copy.generation>best.generation)){best=std::move(copy);best.state=SavedGameFileState::Recovered;}}
    return best;
}
SavedGameEntry NativeMultiplayerStore::describe(const MultiplayerBytes& bytes)const{
    const auto saved=decodeNativeMultiplayerEnvelope(bytes);try{return describe(saved.info,saved.checkpoint);}catch(const std::exception&){SavedGameEntry entry;entry.id=saved.info.id;entry.name=saved.info.title;entry.lastPlayed=saved.info.lastPlayed;entry.players=0;return entry;}
}
SavedGameEntry NativeMultiplayerStore::describe(const NativeMultiplayerInfo& info,const MultiplayerBytes& checkpoint)const{
    const auto c=decodeNativeMultiplayerCheckpoint(*baseline_,checkpoint);const auto& m=*c.memory;SavedGameEntry entry{info.id,info.title,m[0x1c9ce],m[0x1c9d0],info.lastPlayed};
    entry.players=m[0x5eb76]+1;entry.championship=m[0x57c67]+1;entry.competition=c.menu.competitive;entry.finish=c.finishPending;entry.report=c.menu.competitive&&c.menu.page!=NativeMenuPage::Matchup;
    if(entry.competition)entry.race=std::min(unsigned(m[0x5eb7b])+(c.menu.multiplayerTrackEnd?0u:1u),2*entry.players);return entry;
}
SavedGameRead NativeMultiplayerStore::read(const std::string& id)const{
    try{return recoveries(folders_.directory(id)/L"game.scr-multiplayer");}catch(const std::out_of_range&){return {SavedGameFileState::Missing};}
}
bool NativeMultiplayerStore::write(const fs::path& path,NativeMultiplayerSave candidate,std::string& error){
    try{
        SavedGameRead copies[3];for(unsigned n=0;n<3;++n){copies[n]=generation(career_files::generationPath(path,n));
            if(copies[n].state==SavedGameFileState::Unsupported)throw std::runtime_error("An unsupported multiplayer generation is preserved");
            if(copies[n].state==SavedGameFileState::Valid&&decodeNativeMultiplayerEnvelope(copies[n].data).info.id!=candidate.info.id)throw std::runtime_error("Multiplayer saved-game ownership differs");}
        const auto newest=std::max({copies[0].generation,copies[1].generation,copies[2].generation});if(newest==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Multiplayer save generation exhausted");
        const auto stamp=std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        for(unsigned n=0;n<3;++n)if(copies[n].state==SavedGameFileState::Corrupt){const auto file=career_files::generationPath(path,n);if(!MoveFileExW(windowsFilePath(file).c_str(),windowsFilePath(file.wstring()+L".damaged-"+stamp).c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve a damaged multiplayer generation");}
        if(copies[0].state==SavedGameFileState::Valid&&copies[0].generation!=copies[1].generation){
            if(copies[1].state==SavedGameFileState::Valid&&!career_files::atomicWrite(career_files::generationPath(path,2),copies[1].data,error))return false;
            if(!career_files::atomicWrite(career_files::generationPath(path,1),copies[0].data,error))return false;
        }
        candidate.generation=newest+1;return career_files::atomicWrite(path,encodeNativeMultiplayerSave(*baseline_,candidate),error);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
std::filesystem::path NativeMultiplayerStore::save(const NativeMultiplayerInfo& info,const MultiplayerBytes& checkpoint,const fs::path& selected){
    NativeMultiplayerSave saved{info,1,checkpoint};const auto bytes=encodeNativeMultiplayerSave(*baseline_,saved);auto entry=describe(bytes);entry.folder=selected;const auto folder=folders_.reserve(entry),path=folder/L"game.scr-multiplayer";
    worker_.submitSavedGame(path,bytes,[this,path](const auto& data,std::string& error){
        try{const auto candidate=decodeNativeMultiplayerSave(*baseline_,data);const auto folder=path.parent_path();folders_.check(folder);
            if(windowsPathLink(path)||windowsPathLink(career_files::generationPath(path,1))||windowsPathLink(career_files::generationPath(path,2)))throw std::runtime_error("Saved-game file links are not followed");
            const bool okay=write(path,candidate,error);if(okay)folders_.update(describe(data),folder);return okay;
        }catch(const std::exception& failure){error=failure.what();return false;}
    });return folder;
}
std::string NativeMultiplayerStore::lastUsed()const{return loadPlayerSelections(settings_).multiplayer;}
void NativeMultiplayerStore::setLastUsed(const std::string& id){
    if(!id.empty()&&!validSavedGameFolder(id))throw std::invalid_argument("Invalid multiplayer selection");
    worker_.submitSavedGame(preferenceKey(),MultiplayerBytes(id.begin(),id.end()),[this](const auto& data,std::string& error){
        if(savePlayerSelection(settings_,std::string(data.begin(),data.end()),true))return true;error="Cannot save last-used multiplayer preference";return false;});
}
void NativeMultiplayerStore::cancelDraft(const SavedGameEntry& entry,const std::string& lastUsed){
    worker_.flush();worker_.forgetWrites(entry.folder);folders_.cancelDraft(entry);worker_.forgetWrites(preferenceKey());
    if(this->lastUsed()!=lastUsed&&!savePlayerSelection(settings_,lastUsed,true))throw std::runtime_error("Could not restore multiplayer selection");
}
} // namespace scr::runtime

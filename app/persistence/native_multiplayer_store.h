#pragma once
#include "persistence/native_career_store.h"
#include "persistence/native_multiplayer_checkpoint.h"
#include "persistence/saved_game_folders.h"

namespace scr::runtime {
class NativeMultiplayerStore {
public:
    NativeMultiplayerStore(NativeCareerStore& worker,const NativeMemory& baseline,std::filesystem::path settings={});
    ~NativeMultiplayerStore();
    std::vector<SavedGameEntry> list()const{return folders_.list();}
    SavedGameRead read(const std::string&)const;
    std::filesystem::path save(const NativeMultiplayerInfo&,const MultiplayerBytes&,const std::filesystem::path& folder={});
    std::string lastUsed()const;
    SavedGameEntry describe(const NativeMultiplayerInfo&,const MultiplayerBytes&)const;
    void setLastUsed(const std::string&);
    void cancelDraft(const SavedGameEntry&,const std::string& lastUsed);
private:
    NativeCareerStore& worker_;
    std::unique_ptr<NativeMemory> baseline_;
    std::filesystem::path settings_;
    SavedGameFolders folders_;
    SavedGameFolderFormat format();
    SavedGameRead generation(const std::filesystem::path&,bool validate=true)const;
    SavedGameRead recoveries(const std::filesystem::path&)const;
    SavedGameEntry describe(const MultiplayerBytes&)const;
    bool write(const std::filesystem::path&,NativeMultiplayerSave,std::string&);
    std::filesystem::path preferenceKey()const{return worker_.root()/L".multiplayer-preference";}
};
} // namespace scr::runtime

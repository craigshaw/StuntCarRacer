#pragma once
#include "persistence/native_career_store.h"
#include "persistence/saved_game_folders.h"
namespace scr::runtime {
class CareerFolders:public SavedGameFolders {
public:
    CareerFolders(std::filesystem::path,NativeCareerStore::Validator,FolderTransactionObserver);
private:
    void rekey(const std::filesystem::path&,const std::string&,const std::string&);
};
} // namespace scr::runtime

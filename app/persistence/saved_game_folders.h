#pragma once
#include "persistence/saved_game_types.h"
#include <functional>
#include <map>
#include <mutex>

namespace scr::runtime {
struct SavedGameFolderFormat {
    std::filesystem::path category,head;
    std::function<SavedGameRead(const std::filesystem::path&)> read,readGeneration;
    std::function<SavedGameEntry(const SavedGameBytes&)> describe;
};
// Immediate folders are the catalogue. Internal IDs bind checkpoint/PB contents,
// while folder names identify independent copies and the last-played selection.
class SavedGameFolders {
public:
    SavedGameFolders(std::filesystem::path,SavedGameFolderFormat);
    std::vector<SavedGameEntry> list()const;
    std::filesystem::path directory(const std::string& key)const;
    std::filesystem::path reserve(const SavedGameEntry&);
    void refresh();
    void update(SavedGameEntry,const std::filesystem::path&);
    void check(const std::filesystem::path&)const;
    // Only a new-game reservation may be cancelled internally.
    void cancelDraft(const SavedGameEntry&);
private:
    std::filesystem::path root_;
    SavedGameFolderFormat format_;
    mutable std::mutex mutex_;
    std::vector<SavedGameEntry> entries_;
    std::map<std::string,std::filesystem::path> drafts_;
};
} // namespace scr::runtime

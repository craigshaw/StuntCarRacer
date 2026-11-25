#pragma once
#include "persistence/saved_game_types.h"
#include "persistence/folder_transactions.h"
#include <map>
#include <mutex>
#include <set>

namespace scr::runtime {
struct SavedGameFolderFormat {
    std::filesystem::path category,head;
    std::function<SavedGameRead(const std::filesystem::path&)> read,readGeneration;
    std::function<SavedGameEntry(const SavedGameBytes&)> describe;
    std::function<void(const std::filesystem::path&,const std::string&,const std::string&)> rekey;
};
// Discovery and folder transactions are shared; each mode supplies its codec.
class SavedGameFolders {
public:
    SavedGameFolders(std::filesystem::path,SavedGameFolderFormat,FolderTransactionObserver={});
    std::vector<SavedGameEntry> list()const;
    std::filesystem::path directory(const std::string&)const;
    std::filesystem::path reserve(const SavedGameEntry&);
    bool known(const std::filesystem::path&)const;
    void refresh();
    void update(SavedGameEntry,const std::filesystem::path&);
    void recover();
    void create(const std::filesystem::path&,const std::function<void(const std::filesystem::path&)>&);
    std::string resolve(const SavedGameEntry&,SavedGameCopyChoice,const std::string& newId);
    void erase(const SavedGameEntry&);
    void forget(const std::string&);
private:
    std::filesystem::path root_;
    SavedGameFolderFormat format_;
    FolderTransactions transactions_;
    mutable std::mutex mutex_;
    std::vector<SavedGameEntry> entries_;
    std::map<std::string,std::filesystem::path> reserved_;
    std::set<std::wstring> names_;
    static std::wstring folded(std::wstring);
    static std::string label(const std::string&);
    static void conflicts(std::vector<SavedGameEntry>&);
};
} // namespace scr::runtime

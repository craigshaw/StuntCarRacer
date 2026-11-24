#pragma once
#include "persistence/native_career_store.h"
#include <map>
#include <mutex>
#include <set>

namespace scr::runtime {
class CareerFolders {
public:
    CareerFolders(std::filesystem::path root,NativeCareerStore::Validator,FolderTransactionObserver);
    std::vector<NativeCareerInfo> list()const;
    std::filesystem::path directory(const std::string& id)const;
    std::filesystem::path reserve(const NativeCareerInfo&);
    bool known(const std::filesystem::path&)const;
    void refresh();
    void update(NativeCareerInfo,const std::filesystem::path&);
    void recover();
    void create(const std::filesystem::path&,const std::function<void(const std::filesystem::path&)>&);
    std::string resolve(const NativeCareerInfo&,CareerCopyChoice,const std::string& newId);
    void erase(const NativeCareerInfo&);
    void forget(const std::string&);
private:
    std::filesystem::path root_;
    NativeCareerStore::Validator validator_;
    FolderTransactions transactions_;
    mutable std::mutex mutex_;
    std::vector<NativeCareerInfo> entries_;
    std::map<std::string,std::filesystem::path> reserved_;
    std::set<std::wstring> names_;
    static std::wstring folded(std::wstring);
    static std::string label(const std::string&);
    static void conflicts(std::vector<NativeCareerInfo>&);
    void rekey(const std::filesystem::path&,const std::string&,const std::string&);
};
} // namespace scr::runtime

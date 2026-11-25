#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace scr::runtime {
enum class FolderTransactionStage {Prepared,Parked,Published,Committed};
using FolderTransactionObserver=std::function<void(FolderTransactionStage)>;
// The host owns the data-root lock. Game operations admit immediate category
// children; collection import admits fixed managed root entries. Transaction
// work is never discovered as a saved game.
class FolderTransactions {
public:
    explicit FolderTransactions(std::filesystem::path root,FolderTransactionObserver observer={});
    bool recover();
    void publish(const std::filesystem::path& target,const std::vector<std::filesystem::path>& retire,
        const std::function<void(const std::filesystem::path& stage)>& build);
    void erase(const std::filesystem::path& folder);
    // Import publishes a complete set of managed root entries under one journal.
    // The build callback receives an empty directory, never the live data root.
    void publishCollection(const std::function<void(const std::filesystem::path& stage)>& build);
    static void copy(const std::filesystem::path& source,const std::filesystem::path& destination);
    static bool directoryLink(const std::filesystem::path&);
private:
    std::filesystem::path root_,work_;
    FolderTransactionObserver observer_;
    std::string relative(const std::filesystem::path&)const;
    void finish(const std::filesystem::path& transaction);
    void finishCollection(const std::filesystem::path& transaction);
};
} // namespace scr::runtime

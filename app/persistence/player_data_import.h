#pragma once
#include "persistence/player_data_layout.h"
#include "persistence/folder_transactions.h"
#include <set>

namespace scr::runtime {
struct PlayerImportSource {
    std::filesystem::path root,settings,originalHall,enhancedHall;
};
PlayerImportSource playerImportSource(const std::filesystem::path& root,
    const std::filesystem::path& originalHall={},const std::filesystem::path& enhancedHall={});
bool hasPlayerImportSource(const PlayerImportSource&);
struct PlayerImportOptions {bool replaceSettings{},replaceOriginalHall{},replaceEnhancedHall{};};
struct PlayerImportReport {
    unsigned careers{},multiplayer{},conflicts{},skipped{};
    bool assets{},settings{},originalHall{},enhancedHall{};
    std::vector<std::string> warnings;
};
// All source access is read-only. The caller holds the destination root lock.
// Call on a worker; recovery completes before launcher/session reads resume.
class PlayerDataImport {
public:
    explicit PlayerDataImport(std::filesystem::path root,FolderTransactionObserver={});
    bool decided()const;
    void startFresh();
    PlayerImportReport import(const PlayerImportSource&,PlayerImportOptions={});
private:
    PlayerDataLayout layout_;
    FolderTransactions transactions_;
};
} // namespace scr::runtime

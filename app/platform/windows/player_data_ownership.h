#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace scr::runtime {
// Hold before constructing a launcher, until all session/storage workers stop.
// A byte-range file lock works across accounts and Windows logon sessions.
class PlayerDataOwnership {
public:
    explicit PlayerDataOwnership(const std::filesystem::path& root);
    ~PlayerDataOwnership();
    PlayerDataOwnership(const PlayerDataOwnership&)=delete;
    PlayerDataOwnership& operator=(const PlayerDataOwnership&)=delete;
    bool acquired()const noexcept;
    const std::wstring& windowClassName()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace scr::runtime

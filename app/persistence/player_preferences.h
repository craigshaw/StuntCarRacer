#pragma once
#include <filesystem>
#include <mutex>
#include <string>

namespace scr::runtime {
struct PlayerSelections {std::string career,multiplayer;};
bool validPlayerIdentity(const std::string&)noexcept;
bool validSavedGameFolder(const std::string&)noexcept;
bool playerSelectionLine(const std::string&);
// All preference read/modify/write operations in this process share this lock.
std::mutex& playerPreferencesMutex();
PlayerSelections loadPlayerSelections(const std::filesystem::path&);
bool savePlayerSelection(const std::filesystem::path&,const std::string& id,bool multiplayer=false);
bool saveDisplayDocument(const std::filesystem::path&,const std::string& displayBody);
} // namespace scr::runtime

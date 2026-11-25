#pragma once
#include "persistence/hall_storage_types.h"
#include <filesystem>

namespace scr::runtime {
// Resolution is read-only. Session codecs decide how their contents are stored.
struct PlayerDataLayout {
    std::filesystem::path root,pack,settings,careers,multiplayer;
    HallArchiveLocation originalHall,enhancedHall;
};
PlayerDataLayout playerDataLayout(const std::filesystem::path& root);
// The host supplies its absolute executable path, never the working directory.
PlayerDataLayout portablePlayerDataLayout(const std::filesystem::path& executable);
} // namespace scr::runtime

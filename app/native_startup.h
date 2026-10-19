#pragma once
#include "native_asset_initialization.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace scr::viewer {
struct NativeStartupData {
    std::unique_ptr<NativeMemory> memory;
    std::vector<std::uint8_t> payload;
};
// Fresh local Practice: validated pack -> native assets -> game data -> track
// construction -> vehicle placement. No capture/ROM/CPU is an input.
[[nodiscard]] NativeStartupData startNativeRace(const std::filesystem::path& pack,unsigned trackId=0);
} // namespace scr::viewer

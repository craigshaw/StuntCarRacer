#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace scr::viewer {
struct LocalAssetPack {std::vector<std::uint8_t> payload;};
// Validates version, complete extent, source identity and payload SHA-256.
// The source image is data for native decoders; no original code is executed.
[[nodiscard]] LocalAssetPack loadLocalAssetPack(const std::filesystem::path&);
} // namespace scr::viewer

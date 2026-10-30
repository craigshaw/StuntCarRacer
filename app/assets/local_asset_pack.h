#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>

namespace scr::runtime {
struct LocalAssetPack {std::vector<std::uint8_t> payload;};
// Validates version, complete extent, source identity and payload SHA-256.
// The source image is data for native decoders; no original code is executed.
[[nodiscard]] LocalAssetPack loadLocalAssetPack(const std::filesystem::path&);
// Read-only source disk, verified recipe, atomic destination replacement.
void importLocalAssetPack(const std::filesystem::path& adf,const std::filesystem::path& pack);
std::wstring localAssetSourceName(const std::filesystem::path& pack);
std::wstring supportedAdfNames();
} // namespace scr::runtime

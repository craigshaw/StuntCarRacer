#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scr {
struct IndexedImage {
    unsigned width{},height{};
    std::vector<std::uint8_t> pixels;
};
struct DecodedPlanarRle {
    std::vector<std::uint8_t> planes;
    std::size_t consumed{};
};
// Original image decoder0x593f8: PackBits runs restart for each row/plane.
// Layout: complete planes, then rows, then eight MSB-first pixels per byte.
DecodedPlanarRle decodePlanarRle(const std::uint8_t* data,std::size_t size,
    unsigned width,unsigned height,unsigned planeCount);
IndexedImage decodePlanarImage(const std::uint8_t* data,std::size_t size,
    unsigned width,unsigned height,unsigned planeCount);
// Original source palette stores0..7 per channel. copyPaletteToCopperlist
// doubles each channel, then sets its low bit iff nonzero. Output0x00RRGGBB.
std::uint32_t originalPaletteRgb(std::uint16_t source) noexcept;
} // namespace scr

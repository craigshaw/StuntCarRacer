#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace scr {
// Original generated stream: one header and two local X/Z/height vertices per
// cross-section. Words preserve original bits; no game data is embedded here.
struct TrackMeshSection {
    std::uint8_t segment{},flags{};
    std::array<std::array<std::uint16_t,3>,2> points{}; // local X, local Z, height
};
struct TrackMeshData {
    std::uint8_t segmentCount{};
    std::array<std::uint8_t,100> segmentCoordinates{}; // X low nibble, Z high
    std::vector<TrackMeshSection> sections;
};
struct TrackMeshVertex {
    // Simulation high-word units: car worldPosition/65536. X/Z grid cell128;
    // local X/Z divide16, raw road height divides32. Original angular screen
    // projection halves visual Y relative to X/Z; apply that in the camera.
    double x{},y{},z{};
};
struct TrackMeshFace {
    std::array<unsigned,4> indices{};
    std::uint8_t segment{},flags{};
};
struct TrackMesh {
    std::vector<TrackMeshVertex> vertices;
    std::vector<TrackMeshFace> faces;
};
// Converts the original cached cross-sections to a closed strip of road quads.
// Sections preserve their original order. The final cross-section joins the
// first, matching transformCoordinates' wrap to segmentGeometryData. Original
// face metadata comes from the ending cross-section, as in drawTrackLines.
// Per-section flags (bank/transition/marked slope) remain available to drawing.
[[nodiscard]] TrackMesh buildTrackMesh(const TrackMeshData& data);
} // namespace scr

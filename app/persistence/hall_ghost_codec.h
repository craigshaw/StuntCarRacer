#pragma once
#include "persistence/hall_table_codec.h"
#include "race/ghost_lap_data.h"

namespace scr::runtime {
// Immutable, owner-neutral recording. No career, mode or control-history fields.
struct HallGhost {
    HallCatalog catalog;
    HallAttemptId attempt{};
    HallBoardKey board;
    PackedLapTime time{};
    std::int64_t durationMicroseconds{};
    GhostLapBoundary start,finish;
    std::vector<GhostLapSample> samples;
    std::vector<GhostBridgeShape> bridges;
};
constexpr unsigned hallGhostFormatVersion=1;
// Header/checksum 124, samples 36 each, shapes 72 + count + at most 510 words.
constexpr std::size_t hallGhostMaximumBytes=124+36*maxGhostLapSamples+(72+4+510*4)*maxGhostLapBridgeShapes;
class UnsupportedHallGhost:public std::runtime_error {public:using std::runtime_error::runtime_error;};
state::checkpoint::Bytes encodeHallGhost(const HallGhost&);
HallGhost decodeHallGhost(const state::checkpoint::Bytes&);
void requireHallGhostBinding(const HallGhost&,const HallAttempt&,HallCatalog);
} // namespace scr::runtime

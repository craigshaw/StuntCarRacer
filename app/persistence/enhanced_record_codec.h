#pragma once
#include "records/enhanced_records.h"
#include "state/native_checkpoint_io.h"

namespace scr::runtime {
constexpr unsigned enhancedRecordFormatVersion=1;
// Header/counts/checksum 168; lap rows <=120 bytes, race rows <=88 bytes.
constexpr std::size_t enhancedHallTableMaximumBytes=168+(hallBoardCount/2)*hallEntriesPerBoard*(120+88);
// Recording body: time/boundaries/counts 136, samples 36, shapes <=2116,
// recovery offsets 8. PB adds 124 bytes; Hall ghost adds 52 bytes.
constexpr std::size_t enhancedRecordingMaximumBytes=136+36*maxGhostLapSamples+2116*maxGhostLapBridgeShapes+8*maxGhostLapSamples;
constexpr std::size_t enhancedBestMaximumBytes=124+enhancedRecordingMaximumBytes;
constexpr std::size_t enhancedHallGhostMaximumBytes=52+enhancedRecordingMaximumBytes;
struct EnhancedHallGhost {
    EnhancedCatalog catalog;HallAttemptId attempt{};HallBoardKey board;
    EnhancedRecording recording;
};
state::checkpoint::Bytes encodeEnhancedHall(const EnhancedHallOfFame&);
EnhancedHallOfFame decodeEnhancedHall(const state::checkpoint::Bytes&,EnhancedCatalog);
state::checkpoint::Bytes encodeEnhancedBest(const EnhancedBest&);
EnhancedBest decodeEnhancedBest(const state::checkpoint::Bytes&);
EnhancedBest decodeEnhancedBest(const state::checkpoint::Bytes&,const EnhancedBestKey&,EnhancedCatalog);
state::checkpoint::Bytes encodeEnhancedHallGhost(const EnhancedHallGhost&);
EnhancedHallGhost decodeEnhancedHallGhost(const state::checkpoint::Bytes&);
void requireEnhancedGhostBinding(const EnhancedHallGhost&,const EnhancedHallAttempt&,EnhancedCatalog);
} // namespace scr::runtime

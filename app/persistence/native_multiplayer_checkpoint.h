#pragma once
#include "menus/native_menu_flow.h"
#include "records/enhanced_records.h"
#include "records/enhanced_time.h"
#include "state/native_checkpoint_io.h"
#include <memory>

namespace scr::runtime {
class UnsupportedNativeMultiplayerCheckpoint:public std::runtime_error {public:using std::runtime_error::runtime_error;};
using MultiplayerBytes=state::checkpoint::Bytes;
constexpr std::size_t nativeMultiplayerCheckpointMaximumBytes=4*1024*1024;
constexpr std::size_t nativeMultiplayerSaveMaximumBytes=nativeMultiplayerCheckpointMaximumBytes+256;
// Safe continuation only: active races resume the retained prepared matchup.
// Native memory owns roster, controls, damage, scoring, both RNG stores and the
// selected championship. No live race object or pending Hall delivery is saved.
struct NativeMultiplayerCheckpoint {
    std::unique_ptr<NativeMemory> memory;
    NativeMenuContinuation menu;
    bool previewPrepared{},finishPending{};
    unsigned preparedTrack{},preparedLeague{};
    std::vector<std::uint32_t> finishPixels;
    std::optional<EnhancedClockDisplay> finishTiming;
    std::optional<EnhancedRecordNotice> enhancedRecordNotice;
};
MultiplayerBytes encodeNativeMultiplayerCheckpoint(const NativeMemory& baseline,const NativeMultiplayerCheckpoint&);
NativeMultiplayerCheckpoint decodeNativeMultiplayerCheckpoint(const NativeMemory& baseline,const MultiplayerBytes&);

// Host metadata is separate from native driver names and has no gameplay RNG.
struct NativeMultiplayerInfo {std::string id,title;std::uint64_t lastPlayed{};};
struct NativeMultiplayerSave {
    NativeMultiplayerInfo info;
    std::uint64_t generation{};
    MultiplayerBytes checkpoint;
};
void validateNativeMultiplayerInfo(const NativeMultiplayerInfo&);
MultiplayerBytes encodeNativeMultiplayerSave(const NativeMemory& baseline,const NativeMultiplayerSave&);
NativeMultiplayerSave decodeNativeMultiplayerSave(const NativeMemory& baseline,const MultiplayerBytes&);
// Envelope operations handle metadata and framing. The store validates the
// checkpoint before queuing immutable bytes and assigns its generation on write.
MultiplayerBytes encodeNativeMultiplayerEnvelope(const NativeMultiplayerInfo&,std::uint64_t generation,const MultiplayerBytes& checkpoint);
NativeMultiplayerSave decodeNativeMultiplayerEnvelope(const MultiplayerBytes&);
} // namespace scr::runtime

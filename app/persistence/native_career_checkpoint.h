#pragma once
#include "persistence/native_career_store.h"
#include "menus/native_menu_flow.h"
#include <memory>

namespace scr::runtime {
class UnsupportedNativeCheckpoint:public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Host continuation data, separate from the simulation and legacy save codec.
struct NativeCareerCheckpoint {
    std::unique_ptr<NativeMemory> memory;
    NativeMenuContinuation menu;
    bool previewPrepared{},finishPending{};
    unsigned preparedTrack{},preparedLeague{};
    std::vector<std::uint32_t> finishPixels;
    // A main-menu/Practice checkpoint may retain one independent season.
    CareerBytes suspendedSeason;
};
CareerBytes encodeNativeCheckpoint(const NativeMemory& baseline,const NativeCareerCheckpoint&);
NativeCareerCheckpoint decodeNativeCheckpoint(const NativeMemory& baseline,const CareerBytes&);
} // namespace scr::runtime

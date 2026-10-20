#pragma once
#include "native_race_initialization.h"
#include <array>
#include <cstdint>

namespace scr::viewer {
enum class NativeSaveKind : std::uint8_t { Career=0,Multiplayer=1,Records=0x40 };
using NativeSavePayload=std::array<std::uint8_t,512>;

// Return true when the original routine returns with carry clear.
[[nodiscard]] bool encodeDecodeNativeLeague(NativeMemory& memory,std::uint8_t mode);
[[nodiscard]] bool syncNativeSaveTransfer(NativeMemory& memory,std::uint8_t direction);

// Begin before opening the slot menu. Restore after saving or cancelling;
// a successful load keeps its newly decoded career instead.
void beginNativeSaveTransaction(NativeMemory& memory,bool write);
void restoreNativeSaveTransaction(NativeMemory& memory);

// Payloads retain the original transfer block. Begin a transaction first.
[[nodiscard]] NativeSavePayload makeNativeSavePayload(NativeMemory& memory,NativeSaveKind kind);
[[nodiscard]] bool applyNativeSavePayload(NativeMemory& memory,NativeSaveKind kind,const NativeSavePayload& payload);

// The caller presents the original roster after this logical transition.
// False means the original invalid-career or multiplayer fallback reset data.
[[nodiscard]] bool prepareNativeReplay(NativeMemory& memory);
} // namespace scr::viewer

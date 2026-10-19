#pragma once
#include <array>
#include <cstdint>

namespace scr::viewer {
using NativeMemory=std::array<std::uint8_t,0x80000>;

// Memory effects of the original successful load transition, before entering
// game-data initialization. Keeps the original steering integrity gate intact.
void initializeNativeLoadState(NativeMemory& memory);

// Zero RAM plus the locally extracted original payload must already be loaded.
// This is the native counterpart of original initializeGameData, before loading
// a selected track. It deliberately clears mutable game data, not a saved race.
void initializeNativeGameData(NativeMemory& memory);

// Original loadPlayerConfiguration. Exposed separately so the reset can be
// compared against the original instructions without entering a game loop.
void initializeNativePlayerConfiguration(NativeMemory& memory);
void initializeNativeGameTables(NativeMemory& memory);

// Complete a fresh local Practice startup after graphics/sample allocation and
// track loading. Runs the recovered vehicle placement and two warm-up frames.
// No CPU, captured state or ROM is involved in these native routines.
void initializeNativeRace(NativeMemory& memory);
} // namespace scr::viewer

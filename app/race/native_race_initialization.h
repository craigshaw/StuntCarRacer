#pragma once
#include <array>
#include <cstdint>

namespace scr::runtime {
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

// Complete a fresh local Practice or season race after graphics/sample allocation and
// track loading. Runs the recovered vehicle placement and two warm-up frames.
// No CPU, captured state or ROM is involved in these native routines.
void initializeNativeRace(NativeMemory& memory);

// Local original league helpers. The six-round fixture schedule and driver
// traits are read from the asset image; these routines retain the original RNG
// ordering, including results simulated in the other divisions.
void configureNativeSeasonMatch(NativeMemory& memory);
void randomizeNativeSeasonCars(NativeMemory& memory);
void scoreNativeSeasonRound(NativeMemory& memory);
void encodeNativeSeason(NativeMemory& memory);
void transferNativeLapRecords(NativeMemory& memory,bool load);
void prepareNativeHallOfFame(NativeMemory& memory);
void initializeNativeSeason(NativeMemory& memory);
// Skips original computer-only fixtures. False means all six rounds ended.
[[nodiscard]] bool seekNativeSeasonRace(NativeMemory& memory);
// Original F1 branch: score all remaining fixtures, then leave promotion to
// the normal season-end caller. No race or per-race result screens are entered.
void forfeitNativeSeason(NativeMemory& memory);
void advanceNativeSeasonRound(NativeMemory& memory);
void finishNativeSeason(NativeMemory& memory);
} // namespace scr::runtime

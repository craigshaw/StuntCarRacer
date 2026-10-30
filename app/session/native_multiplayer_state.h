#pragma once
#include "race/native_race_initialization.h"

namespace scr::runtime {
enum class NativeMultiplayerTrackAdvance { NextTrack,ChampionshipComplete,ChampionshipReset };

void randomizeNativeMultiplayerCars(NativeMemory& memory);
void initializeNativeMultiplayerTrack(NativeMemory& memory);
void initializeNativeMultiplayerSeason(NativeMemory& memory);
void configureNativeMultiplayerRace(NativeMemory& memory);
void prepareNativeMultiplayerRace(NativeMemory& memory);
void loadNativeMultiplayerDamage(NativeMemory& memory);
void finishNativeMultiplayerRaceExit(NativeMemory& memory);
void transferNativeMultiplayerControls(NativeMemory& memory,bool load);
void sortNativeMultiplayerStandings(NativeMemory& memory);
void scoreNativeMultiplayerRace(NativeMemory& memory);
void awardNativeMultiplayerTrackBonuses(NativeMemory& memory);
void forfeitNativeMultiplayerTrack(NativeMemory& memory);

// Advance after all per-race pages; false means the track summary is next.
[[nodiscard]] bool advanceNativeMultiplayerRace(NativeMemory& memory);
// Advance after the track summary and overall standings pages.
[[nodiscard]] NativeMultiplayerTrackAdvance advanceNativeMultiplayerTrack(NativeMemory& memory);
} // namespace scr::runtime

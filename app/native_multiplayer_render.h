#pragma once
#include "native_menu_render.h"

namespace scr::viewer {
enum class NativeMultiplayerScreen { Matchup,Records,RoundResults,Standings,RaceResults,TrackResults };
[[nodiscard]] NativeMenuImage makeNativeMultiplayerImage(const NativeMemory&,
    NativeMultiplayerScreen,unsigned division=~0u,unsigned context=~0u,unsigned entryYOffset=0);
void applyNativeMultiplayerScreenState(NativeMemory&,NativeMultiplayerScreen,unsigned entryYOffset=0);
} // namespace scr::viewer

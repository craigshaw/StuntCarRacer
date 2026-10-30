#pragma once
#include "menus/native_menu_flow.h"

namespace scr::runtime {
// Original local race pages, composed from the extracted menu, portrait,
// result-picture, font and text assets. No original CPU is linked at runtime.
[[nodiscard]] NativeMenuImage makeNativeCompetitionImage(const NativeMemory&,
    NativeMenuPage,unsigned competitionDivision,unsigned competitionContext=~0u);
} // namespace scr::runtime

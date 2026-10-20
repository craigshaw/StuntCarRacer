#pragma once
#include "native_menu_flow.h"

namespace scr::viewer {
// Original local race pages, composed from the extracted menu, portrait,
// result-picture, font and text assets. No original CPU is linked at runtime.
[[nodiscard]] NativeMenuImage makeNativeCompetitionImage(const NativeMemory&,
    NativeMenuPage,unsigned competitionDivision,unsigned competitionContext=~0u);
} // namespace scr::viewer

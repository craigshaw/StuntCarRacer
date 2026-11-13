#pragma once
#include "menus/native_menu_flow.h"
#include "records/enhanced_records.h"

namespace scr::runtime {
// Original local race pages, composed from the extracted menu, portrait,
// result-picture, font and text assets. No original CPU is linked at runtime.
[[nodiscard]] NativeMenuImage makeNativeCompetitionImage(const NativeMemory&,
    NativeMenuPage,unsigned competitionDivision,unsigned competitionContext=~0u);
[[nodiscard]] NativeMenuImage makeEnhancedRecordNoticeImage(const NativeMemory&,
    unsigned competitionDivision,const EnhancedRecordNotice&);
} // namespace scr::runtime

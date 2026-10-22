#pragma once
#include "state/race_transitions_adapter.h"
#include "race_timing_fixture.h"
#include <array>

namespace scr::test::transitions {
using namespace scr::state::transitions;
using Range=timing::Range;
constexpr std::array<Range,11> codeRanges{{{0x5d4ba,0x5d674},{0x5d9dc,0x5dabe},{0x5dac0,0x5db34},
    {0x1b7b6,0x1b7dc},{0x57100,0x57122},{0x612be,0x612ce},{0x5e4ec,0x5e508},{0x57964,0x579b0},{0x5e0aa,0x5e0c4},
    {0x5d674,0x5d724},{0x6368c,0x636a2}}};
} // namespace scr::test::transitions

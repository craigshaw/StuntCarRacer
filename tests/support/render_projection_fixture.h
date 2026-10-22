#pragma once
#include "state/render_projection_adapter.h"
#include "race_timing_fixture.h"
#include <array>

namespace scr::test::renderProjection {
using namespace scr::state::renderProjection;
using Range=timing::Range;
constexpr std::array<Range,7> codeRanges{{{0x6249c,0x62574},{0x64d08,0x64e18},{0x61368,0x6158c},
    {0x600a6,0x60246},{0x5ff94,0x6001c},{0x65ec4,0x6607c},{0x1ca42,0x1ec46}}};
} // namespace scr::test::renderProjection

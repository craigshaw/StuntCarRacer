#pragma once
#include "state/motion_integration_adapter.h"
#include <array>
#include <cstdint>

namespace scr::test::integration {
using namespace scr::state::integration;
struct Range { unsigned begin,end; };
constexpr std::array<Range,3> stateRanges{{{0x1bcd8,0x1bcf6},{0x1bbab,0x1bbac},{0x1bc42,0x1bc44}}};
constexpr std::array<Range,5> inputRanges{{{0x1bcf6,0x1bd02},{0x1bd3a,0x1bd40},
    {0x1bb75,0x1bb76},{0x1bb9a,0x1bb9b},{0x61ad4,0x61adc}}};
constexpr unsigned positionEntry=0x61950,positionEnd=0x61ad4;
constexpr unsigned worldEntry=0x61adc,worldEnd=0x61b26;
constexpr unsigned angularEntry=0x61b26,angularEnd=0x61b70;
constexpr unsigned continuousEntry=0x618c0,continuousEnd=0x618ce;
} // namespace scr::test::integration

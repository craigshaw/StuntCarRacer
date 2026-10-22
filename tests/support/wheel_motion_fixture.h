#pragma once
#include "state/wheel_motion_adapter.h"
#include <array>
#include <cstdint>

namespace scr::test::wheelMotion {
using namespace scr::state::wheelMotion;
struct MemoryRange { unsigned begin, end; };
constexpr unsigned speedEntry=0x60fbe, speedEnd=0x61012;
constexpr unsigned dampingEntry=0x62138, dampingEnd=0x6217a;
constexpr std::array<MemoryRange,2> codeRanges{{{speedEntry,speedEnd},{dampingEntry,dampingEnd}}};
// Compare every written byte and nearby untouched words, including the enhanced
// reference's wheelSpeed address 0x1bc60 and angularAccelerationY at 0x1bcfe.
constexpr std::array<MemoryRange,3> observedRanges{{
    {0x1bc60,0x1bc66},{0x1bd5a,0x1bd60},{0x1bcfa,0x1bd04},
}};
constexpr std::array<MemoryRange,7> inputRanges{{
    {0x1bd30,0x1bd32},{0x1bb7e,0x1bb7f},{0x1bcf0,0x1bcf2},
    {0x1bcf4,0x1bcf6},{0x1bd26,0x1bd2a},{0x1bd36,0x1bd38},{0x1bc62,0x1bc64},
}};
constexpr std::array<unsigned,2> speedOutputs{0x1bd5c,0x1bc62};
constexpr std::array<unsigned,2> dampingOutputs{0x1bcfc,0x1bd00};
} // namespace scr::test::wheelMotion

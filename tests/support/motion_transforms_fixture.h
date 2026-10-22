#pragma once
#include "state/motion_transforms_adapter.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace scr::test::motion {
using namespace scr::state::motion;
constexpr std::array<unsigned,4> entries{0x6158c,0x615e6,0x61618,0x61672};
constexpr std::array<unsigned,4> ends{0x615e6,0x61618,0x61672,0x616d6};
constexpr std::array<unsigned,9> inputWords{0x1bcea,0x1bcec,0x1bcee,0x1bd32,0x1bd34,0x1bd36,0x1bcf0,0x1bcf2,0x1bcf4};
constexpr std::array<unsigned,11> outputWords{0x1bd2c,0x1bd30,0x1bd0e,0x1bd10,0x1bd12,0x1bcf6,0x1bcf8,0x1bcfa,0x1bd3a,0x1bd3c,0x1bd3e};
constexpr std::array<std::array<unsigned,2>,4> observedRanges{{{0x1bcf6,0x1bcfc},{0x1bd0e,0x1bd14},{0x1bd2c,0x1bd32},{0x1bd3a,0x1bd40}}};
} // namespace scr::test::motion

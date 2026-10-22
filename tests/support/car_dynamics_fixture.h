#pragma once
#include "state/car_dynamics_adapter.h"
#include <array>
#include <cstdint>

namespace scr::test::carDynamics {
using namespace scr::state::carDynamics;
struct Range { unsigned begin,end; };
constexpr std::array<unsigned,4> entries{0x620b8,0x6217a,0x621da,0x621f4};
constexpr std::array<unsigned,4> ends{0x62138,0x621da,0x621f4,0x622dc};
constexpr std::array<Range,3> observedRanges{{{0x1bbc0,0x1bbc4},{0x1bd28,0x1bd3a},{0x1bcf4,0x1bcfe}}};
constexpr std::array<unsigned,7> wordOutputs{0x1bd2a,0x1bd32,0x1bd34,0x1bd36,0x1bcf6,0x1bcf8,0x1bcfa};
constexpr unsigned byteOutput=0x1bbc1;
} // namespace scr::test::carDynamics

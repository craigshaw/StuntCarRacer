#pragma once
#include "track_sides.h"
#include "render_projection_fixture.h"
#include "track_mesh_fixture.h"
namespace scr::test::sides {
using renderProjection::word;using renderProjection::longword;using renderProjection::putWord;using renderProjection::putLong;
constexpr std::array<timing::Range,11> codeRanges{{{0x65fca,0x6607a},{0x6249c,0x62574},
    {0x64d08,0x64e18},{0x68a56,0x68f28},{0x691b4,0x6932c},{0x67460,0x67708},
    {0x1ca42,0x1ec46},{0x691b2,0x691b4},{0x673ee,0x67438},
    {0x668a8,0x66fb8},{0x67740,0x67888}}};
inline TrackSideStyle readStyle(const std::uint8_t* r){return {r[0x691b3],r[0x691b2],false};}
} // namespace scr::test::sides

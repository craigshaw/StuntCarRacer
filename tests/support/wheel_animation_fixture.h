#pragma once
#include "state/wheel_animation_adapter.h"
#include <cstddef>
#include <cstdint>

namespace scr::test::wheelAnimation {
using namespace scr::state::wheelAnimation;
inline constexpr unsigned vblankEntry=0x616dc,vblankEnd=0x6171a;
inline constexpr unsigned graphicsEntry=0x651b8,graphicsEnd=0x6521c;
inline constexpr unsigned rotationEntry=0x5e744,rotationEnd=0x5e778;
inline constexpr unsigned spriteEntry=0x69ee0,spriteEnd=0x69f04;
} // namespace scr::test::wheelAnimation

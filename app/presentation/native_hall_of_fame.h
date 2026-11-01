#pragma once
#include "presentation/native_menu_render.h"

namespace scr::runtime {
[[nodiscard]] NativeMenuImage makeNativeHallOfFameBackground(const NativeMemory&);
[[nodiscard]] NativeMenuImage makeNativeHallOfFameImage(const NativeMemory&);
} // namespace scr::runtime

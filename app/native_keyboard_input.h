#pragma once
#include <cstdint>
#include <optional>

namespace scr::viewer {
struct NativeRawKeyEvent { std::uint8_t code{};bool pressed{}; };
// Windows virtual keys are mapped to Amiga key positions. Character/case
// translation belongs to the original tables in the local asset image.
[[nodiscard]] std::optional<std::uint8_t> nativeAmigaRawKey(
    unsigned virtualKey,unsigned scanCode=0,bool extended=false);
}

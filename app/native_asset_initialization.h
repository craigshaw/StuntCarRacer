#pragma once
#include <array>
#include <cstdint>

namespace scr::viewer {
using NativeMemory=std::array<std::uint8_t,0x80000>;
// Start with zeroed memory and the original ADF payload at 0xe700. Rebuild
// startup-generated graphics, PCM, font and frame buffers from those bytes.
// Captured emulator state is neither accepted nor consulted.
void initializeNativeAssets(NativeMemory& memory);
} // namespace scr::viewer

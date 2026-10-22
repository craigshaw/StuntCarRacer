#pragma once
#include "native_asset_initialization.h"

namespace scr::viewer {
// Original race-entry0x60988: restore all ten intact/damaged chassis sections
// in both buffers. The damage routine later reads these pixels as occupancy.
void initializeNativeDamageHoles(NativeMemory&);
} // namespace scr::viewer

#pragma once
#include <array>
#include <cstdint>

namespace scr {
using NativeMemory=std::array<std::uint8_t,0x80000>;
// Construct the original track topology, interpolation bases, cell lookup,
// visibility hints and render cache from the encoded database in the local
// payload. RAM must contain the unmodified payload at0xe700. No savestate,
// generated geometry or emulated processor is required. trackId is0..7.
void initializeNativeTrack(NativeMemory& memory,unsigned trackId);
// Original cache generation0x65bec, after the preview's first bridge update.
void rebuildNativeTrackGeometryCache(NativeMemory& memory);
} // namespace scr

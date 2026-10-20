#pragma once
#include "native_race_initialization.h"
#include "race_timing.h"

namespace scr::viewer {
// Native counterparts of the original planar cockpit drawing leaves. Assets,
// glyphs and object parameters are read from the initialized local asset image.
// These functions retain the original cursor/scratch writes and update both
// original screen buffers unless the original single-buffer flag is enabled.
// Call them on presentation RAM when the gameplay event occurs, rather than
// recomputing the HUD from a later physics state at display refresh time.
void drawNativeCockpitCharacter(NativeMemory&,unsigned character);
void drawNativeCockpitLabels(NativeMemory&);
void drawNativeCockpitLapCounter(NativeMemory&);
void drawNativeCockpitBoostDigits(NativeMemory&);
void drawNativeCockpitLapTime(NativeMemory&,unsigned visibility,unsigned position,unsigned statisticsIndex);
void drawNativeCockpitLapTime(NativeMemory&,PackedLapTime,unsigned visibility,unsigned position,unsigned statisticsIndex);
void drawNativeCockpitLeaderIcon(NativeMemory&);
void drawNativeCockpitLeaderIcon(NativeMemory&,unsigned originalCarId);
void drawNativeCockpitResultIcon(NativeMemory&);
void drawNativeCockpitResultIcon(NativeMemory&,unsigned originalCarId);
void drawNativeCockpitDistance(NativeMemory&);
void updateNativeCockpitSpeedometer(NativeMemory&);
// Refresh only gameplay inputs consumed by the leaves. Preserves both planar
// buffers, the text cursor/masks, and the speedometer's previous length.
void syncNativeCockpitInstrumentInputs(NativeMemory& presentation,const NativeMemory& gameplay);
// Original race-entry labels and initial car icons (both show the opponent
// until the normal leader/finish updates). Boost digits are a separate event.
void initializeNativeCockpitInstruments(NativeMemory&);
} // namespace scr::viewer

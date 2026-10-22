# Original palette fades

`app/native_palette_fade.cpp` ports the original palette transition routines
`fadeToColor` (`0x6444e`) and `animatePaletteToTarget` (`0x64470`). Addresses refer
to the supported disk payload loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.

The current palette is sixteen source words at `0xe83a`; the target is at
`0xe85a`. Each RGB channel has the original 0..7 range. The existing verified
`originalPaletteRgb` conversion reproduces `copyPaletteToCopperlist` (`0x1ba82`):
double each channel, set its low bit if nonzero, then expand the resulting
four-bit channel for native RGB output.

Each fade pass moves every differing source channel one step toward its target.
If any channel changes, the original copies the palette into the copper list,
sets `0x616d8` to two and waits for the PAL interrupt to decrement it to zero.
The first pass runs immediately; subsequent passes are separated by two vblanks
(40 ms on the recovered 50 Hz PAL fade clock). A final
two-vblank wait still occurs after the pass that reaches the target.

`fadeToColor` additionally writes its flat destination into current entry zero,
fills all target entries with that colour, and sets `0x616da` to thirty. It
finishes only when both the channel progression and this thirty-vblank minimum
have completed. Thus the supported palettes fade out for 600 ms, even when they
already match. A subsequent fade in does not start a fresh thirty-vblank wait;
its duration is two vblanks times the largest channel difference.

Logical and displayed palettes are separate on purpose. Changing entry zero
alone does not update the copper list: only a pass that finds a differing
channel invokes the copy. The helper preserves this original edge case.

`NativePaletteFade::advance` accepts finite deltas from zero through sixty
seconds and accumulates the 50 Hz boundaries. It preserves partial-frame time
between fades. `reset` establishes a new initial palette and clock phase;
`fadeToColor` and `animateTo` require the preceding fade to finish.
`rgbPalette()` is the displayed palette, suitable for rendering indexed pixels.
It should not be approximated by recolouring an already converted RGB image:
different original indices can share the same starting RGB colour.

## Original-code verification

The optional `native_palette_fade_tests` target executes the unmodified original
fade routines and copper conversion through the test-only Musashi MC68000
library. At either original busy-wait loop the harness suspends CPU context,
executes the actual interrupt countdown instructions at `0x616e0..0x61700`, then
restores the suspended context. No original instruction is patched or replaced
with a native implementation. Every source/target/copper palette and countdown
is compared at entry and after each vblank.

```powershell
cmake --build build/windows-oracle --config Release --target native_palette_fade_tests
build/windows-oracle/Release/native_palette_fade_tests.exe analysis/reference/vesuri/offline-extracted-payload.bin
```

The recorded run passed **397 cases**, comparing **628,848 values** across
**12,704 original vblanks** and 2,031,317 original instructions, with zero
mismatches. It covers every 0..7 source/destination pair for all three channels,
fade-out and fade-in paths, the actual cockpit/menu/preview palettes, and the
entry-zero edge case. A separate check verifies that fractional presentation
deltas produce the same palette as explicit vblank progression.

`analysis/native-menu/palette-check-results.json` records source and executable
hashes. REA evidence is retained in
`analysis/rea/native-palette-functions.json`; raw Capstone MC68000 decoding and
the execution comparisons corroborate the recovered behavior. These tests
verify palette progression and timing; the application's transition controller
separately determines which frozen image is displayed at each transition.

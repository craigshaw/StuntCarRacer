# Native menu rendering

`app/native_menu_render.cpp` reconstructs the original 320×200 menu from the
local asset pack. It reads the image, palette, font, strings, strip offsets and
track names from `NativeMemory`; no screenshot, host font, copied text table or
captured emulator state is a runtime input.

`NativeMenuRenderState::disabledRows` optionally marks unavailable native rows.
They retain their unselected strip and use grey text (palette index 3), even if
the caller supplies their index as selected. The mode chooser uses this for
Computer Link; the default empty mask preserves original menu pixels.

The supported original disk payload has SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Runtime addresses below use its verified load base `0xe700`, not disk offsets.

| Original address | Role |
| --- | --- |
| `0x23c32` | Sixteen source palette words, converted by the existing verified `originalPaletteRgb` routine |
| `0x23c52` | 320×200 menu image, four interleaved plane words per group of sixteen pixels |
| `0x1fe82` | Original seven-pixel-wide, eight-row font, including the native startup patches |
| `0x5a69a`, `0x5eb7f` | Ordinary and alternate menu string tables |
| `0x5bcd0` | Menu option string offsets |
| `0x64354` | Positioned division/track header strings |
| `0x64af4` | Option row positions |
| `0x64c1a` | Original normal, selected and confirmed strip source offsets |
| `0x648c2`, `0x1edaa` | Division-to-track mapping and fifteen-character track names |

The original image already contains the title, timber frame, landscape and row
artwork. The native renderer copies it, reproduces the filled central area of
`drawScreenFrame` (`0x64c6e`), and places the original strips over it using the
same source offsets as `0x64b0e`. Selection and confirmation use different source
strips. Strings retain their original `0x1f,column,row` positioning and `0xff`
termination, with the menu display's eight-pixel vertical displacement.
The cursor advances seven pixels, while glyph bytes retain all eight original
bits. This matters for the plus sign used in the control messages.

`NativeMenuRenderState::count` is the original `displayMenu` D2 parameter. The
original paints **count + 1** rows, including the final Back/Cancel option. This
preserves the original's inclusive selection range, rather than treating its
stored maximum index as a conventional item count.

## Original-code comparison

The optional `native_menu_render_tests` target runs unmodified original MC68000
instructions through the test-only Musashi library. It executes the image copy
at `0x64570` up to `0x645ac`, then `displayMenu` at `0x5b840` up to the completed
render boundary `0x5b962`. Confirmation executes the original repaint entry
`0x5b87e`. The harness changes the CPU's PC only at those stop boundaries; it
does not patch any original instruction or emulate menu input.

The original colour-mask routine updates its own graphics operand at `0x66475`;
the harness permits this original write. The original menu pixels are decoded
from the planar frame buffer and compared with the native indexed pixels.

```powershell
cmake --build build/windows-oracle --config Release --target native_menu_render_tests
build/windows-oracle/Release/native_menu_render_tests.exe analysis/reference/vesuri/offline-extracted-payload.bin
```

The menu checks compared **11,840,000 pixels across 185 cases**, with zero
mismatches. Cases include the complete
background, every initial game-mode/main/division/track option, all relevant
current/selected division combinations, and both selected and confirmed states.
The same test also verifies the original in-race message renderer. The caller
composes original masked graphics object 51 for its panel, then
`drawNativeGameMessage` reproduces the table at `0x5e65c`, three-character groups,
line positioning, spacing and black/white blink colours. Its original comparison
executes `0x5e540` up to `0x5e64e` with display mode `0x40`; activation, pause and
blink timing remain the caller's responsibility. All fifteen message starts,
one through four groups and both colours contribute another 120 cases.

The combined run compared **19,520,000 pixels across 305 cases**, with zero
mismatches and 24,161,767 original instructions.
`analysis/native-menu/render-check-results.json` records source/executable hashes
and the original payload identity. This test proves these static menu frames;
interaction timing, transitions, masked-panel composition and preview geometry
have separate checks.

REA 6.7.0 / Ghidra 12.1.4 inspection was cross-checked with Capstone MC68000
decoding and then the pixel comparisons. Retained evidence is in
`analysis/rea/native-menu-functions.json` under `menuScreen`, `menuDisplay`,
`menuStrip` and `menuChar`; the imported upstream labels were used to locate
candidates, not as proof of behavior.

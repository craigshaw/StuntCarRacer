# Original startup and name-entry graphics

`NativeOpeningNameScreen` composes the original name-entry screen from the local
asset image and applies accepted glyph events incrementally. `NativeOpeningFlow`
and `NativePlayerNameEntry` determine when those events happen; host repainting
does not advance the cursor, repeat a key or alter the text. The native game does
not execute the verification CPU.

The screen uses the same original menu image, palette and seven-pixel font as
the main menu. Its frame is `drawScreenFrame` at `0x64c6e`, its `NAME?` string is
the league string at `0x64354+0xe0`, and its empty name panel is cursor object 2
on menu item 1. Original `drawHorizontalLine` draws colour 10 at y=133, from
x=106 through x=189. There is no host font or captured screen in this path.

The input arrow begins at column 14, row 16, with the original four-pixel text
offset. Normal glyphs preserve transparent text mode. The original erase glyph
`0x7f` temporarily clears an opaque seven-pixel cell with colour 2 and restores
transparent mode. The complete eight bits of each font row are preserved even
though character advance is seven pixels. Thus erase, spaces and successive
glyphs retain the same edge behaviour as the original. Space padding changes
the stored player name without drawing more text. A blank submission retains
the screen, resets its position and draws the arrow for another attempt.

Mode selection and the add-driver question reuse `makeNativeMenuImage`, with
original menu offsets `0x10` and `0x14`. Both selected and confirmed panels use
the original strip graphics. The logo and scenery are part of the original
menu image; this renderer does not introduce a separate title illustration.

`makeNativeOpeningStandingsImage` renders the startup table for 2–8 local
drivers after `prepareNativeOpeningLocalStandings` has prepared the order and
scores. It follows the normal startup branch of `0x646e8` / `0x5e93e`, including
the original write of 16 to the row byte at `0x64b02`. The table stretches its
panel by copying the original overlapping strip rows. Names and decimal
columns retain the original spacing and palette. Later race-result and
multiplayer timing tables are separate screens.

## Evidence and verification

The source is the supplied 408,540-byte ADF payload loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Runtime asset addresses are `0x23c52` for menu pixels, `0x23c32` for the palette,
and `0x1fe82` for the initialized primary font. Font patches and asset decoding
remain in `initializeNativeAssets`; copyrighted bytes stay in the ignored local
asset pack and analysis output.

REA's original name-entry dossier is `analysis/rea/opening-2.json`, evidence
`ev_01868ff2a11dcd206bbfae2022d06f665e6c0deab2b1cc81bd437570651e1f6a`.
MC68000 Capstone decoding independently checked `0x5b648..0x5b83c`,
`0x64b0e..0x64c18`, `0x6642a..0x6646c`, and the standings drawing calls.
The enhanced reference assembly changes blank-name handling; the native path
uses the supplied disk's actual retry branch.

`native_opening_render_tests` executes the unchanged original drawing routines
in the isolated 68000 verifier and compares every indexed pixel. It checks both
buffers before input and the active display buffer during editing. Its 985
cases compare 63,296,000 pixels and all 16 menu palette entries per case, with
zero mismatches after 31,788,462 original instructions. Coverage includes 40
mode/add-driver menu states, 833 name-panel/glyph/erase/retry checks, and 112
local standings variations across all player counts and divisions, with mixed
case, punctuation and one/two-digit statistics. The local receipt, source hashes
and image exports are under `analysis/opening-render/`.

```powershell
cmake --build build/windows-oracle --config Release --target native_opening_render_tests
& build/windows-oracle/Release/native_opening_render_tests.exe analysis/reference/vesuri/offline-extracted-payload.bin
```

This comparison establishes drawing and palette equivalence at the original
routine boundaries. Input acceptance and release timing are checked by the
name-entry and opening-flow verifiers; Windows presentation and palette fades
are exercised by the integrated native game tests.

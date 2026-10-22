# Native near-track visibility and edge highlights

The viewer's nearest three segments use the original dense geometry and
visibility rules, followed by the recovered ordered road, side and edge
commands. Native C++ produces these commands before gameplay advances through
collision effects, suspension sprites, timing and race transitions. Playback
retains compact command lists and camera snapshots; it executes no 68000 code.
The default game and viewer now combine this stage with the recovered far painter
in both Practice and races; see [shared scene commands](native-scene-commands.md).

Smooth playback rebuilds this near stage for intermediate camera poses using
copies of the saved geometry history and a private track-position query. Those
presentation samples are outside the original-code comparison below. Exact
verified ticks continue to use their retained commands, and interpolation cannot
modify gameplay or the original-rate physics trace. See
[the viewer guide](native-replay-viewer.md) for the 60 FPS playback controls.

The unchanged source is the ADF payload at disk offset `0xdc00`, length 408,540,
loaded at runtime `0xe700`. Payload SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Verifiers record original runtime, payload and disk offsets, code/data hashes,
capture identities and pinned disassembly provenance. Copyrighted geometry,
images and captured RAM remain in ignored local directories.

| Recovered behavior | Original runtime addresses |
| --- | --- |
| Dense segment generation and projection | `0x6521c`, `0x65756` |
| Saved edge history and coordinate carry | `0x654c2`, `0x65564` |
| Side visibility and forced lower vertices | `0x655ea`, `0x65720` |
| Initial viewport cut geometry | `0x65a18` through `0x65bec` |
| Queue construction and depth colors | `0x6702e` through `0x67460` |
| Initial boundary closure | `0x681d6` through `0x6847c` |
| Side/road painter and highlight selection | `0x688fc` through `0x690bc` |

Dense near heights use the original arithmetic shift by two; far mesh heights
use a shift by three. Two saved unrolled endpoint X words alias the tail of the
coordinate buffer at `0x1c0a0` and `0x1c0a2`. They persist through all three
passes and between frames. Negative progress advances the starting segment and
clears only the high progress byte, preserving its fractional byte. The third
pass sets the continuation flag before projection.

Each queue entry carries seven edges and its segment, depth color, processed
flags and steering flags. Original near-line clipping metadata controls queue
admission and boundary closure. Its line buffer is 9,000 bytes; the enhanced
source declares 10,000. The original lateral-offset word is at `0x1bc5c`, two
bytes after the enhanced source equate. The source verifier records the three
affected operands explicitly, with no operand wildcards.

Longitudinal edge colors follow the original depth thresholds and alternating
segment/section flag. Conditional white cross edges and brown side posts follow
the original painter order. Lower vertices are materialized only when the
visibility rules require them. Coarse far geometry is excluded for every
visited near segment so it cannot reintroduce a face culled by near visibility.

## Differential evidence

The geometry oracle passes **21,451 original executions**, including all 1,463
replay poses with persistent history, initial cut edges, forced lower vertices,
random camera rotations, preview contexts and synthetic word-format geometry.
It executes 15,678,016 original instructions with zero mismatches. All 591
compared source instructions match without operand wildcards.

The visibility oracle passes **28,081 original executions** and 35,821,208
original instructions with zero mismatches. It includes 3,729 clipping cases,
2,000 visibility cases, 1,000 queue cases and the complete 1,463-frame replay:
4,389 segment passes and 49,920 ordered painter commands across its fixtures.
The full-frame checks execute original dense and forced-lower projection,
initial cuts, visibility, queue construction and painter boundary selection.
The source comparison covers 936 instructions, with the three concrete lateral
operand differences described above.

The original blitter is intercepted after boundary and command selection.
Native polygon coverage and line pixels remain presentation choices. Native
coverage regressions check offscreen boundary closure, suppression of culled
near faces, clipped highlight spans and later fills occluding earlier lines.
They supplement the existing mountain-notch and near-plane road coverage checks.
These proofs do not establish original pixel equality, interrupt timing, audio,
human control quality or unexercised opponent rendering.

Build the targets using [the viewer guide](native-replay-viewer.md), then run:

```powershell
. .\scripts\env.ps1
& $AmigaPython tools/research/reference/verify_near_track_geometry.py `
  --oracle build/windows-oracle/Release/near_track_geometry_oracle.exe
& $AmigaPython tools/research/reference/verify_near_track_visibility.py `
  --oracle build/windows-oracle/Release/near_track_visibility_oracle.exe
& .\build\windows-oracle\Release\software_renderer_tests.exe `
  .\analysis\reference\vesuri\offline-extracted-payload.bin
& $AmigaPython scripts/check_native_rendering.py
```

The verifiers retain `reference-comparison.json`, `original-disassembly.asm`
and `oracle.json` under `analysis/near-track-geometry/` and
`analysis/near-track-visibility/`. Both near-track oracles include the authored
race when invoked through their verifier. The visibility report records the
control and native helper identities. The viewer report renders every tick,
checks the unchanged gameplay trace and records near queue, fill and visible
highlight counts, alongside eight exported QA frames.

REA evidence is retained in `analysis/rea/near-track-evidence.json` (117 records)
and the closed session in `analysis/rea/near-track-snapshot.json`. Extracted
dossiers are under `analysis/near-track/`, including the original geometry,
visibility, segment queue, painter and boundary-closure procedures. No generated
binary, captured RAM, original geometry array or REA database is tracked.

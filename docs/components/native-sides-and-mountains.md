# Native sidewalls and mountains

The replay viewer draws the original raised-road sides and mountain horizon
using native code. Gameplay still advances independently and is compared with
the retained original execution. The viewer contains no 68000 emulator.

Original inputs remain local and unchanged. These routines use the ADF payload
at disk offset `0xdc00`, length 408,540, loaded at runtime `0xe700`. Its SHA-256
is `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Reference verifiers record runtime, payload and disk offsets, code/data hashes,
capture identities and the pinned source comparison. Copyrighted geometry is
decoded from local inputs rather than embedded in tracked source.

| Recovered behavior | Original runtime addresses | Evidence |
| --- | --- | --- |
| Side lower vertices and admission | `0x65ec4` through `0x6607a` | `analysis/track-sides/rea-producer.json` |
| Far left and right side panels | `0x691b4`, `0x69270` | `analysis/track-sides/rea-{left,right}-panel.json` |
| Strip edges and header ownership | `0x67460` through `0x6770a` | `analysis/track-sides/reference-comparison.json` |
| Mountain selection and projection | `0x6953e` through `0x696fc` | `analysis/mountain-horizon/rea-render.json` |
| Mountain list loader | `0x696fc` through `0x69736` | `analysis/mountain-horizon/reference-comparison.json` |

The original calls the side panels barriers. They share X/Z with the upper
road edge and extend to raw height `0x0200`, or world Y16. The captured track has
102 such quads. Far side fill is palette15 on even segments and the loaded
`trackColorIndex2` on odd segments. Road fill is palette1/2 by segment parity,
except the black-surface flag. The header belongs to the cross-section ending
each strip. Original edge construction verifies that ownership for all 51
strips, including the wrap.

The side oracle passes **9,499 original executions**, including 6,120 lower-vertex
admission cases, 3,072 material cases and full edge construction for the retained
mesh. The source comparison matches 399 original instructions without operand
wildcards. Its branch and material contexts are synthetic; geometry and material
checks do not establish complete visibility-queue or raster equality.

The mountain table at `0x699b8` describes five templates with 25 variants. The
placement table at `0x69a80` selects 32 positions. The disk loader reads the track
ID and immediately replaces it with zero; the two placement pointers share one
list. The pinned source comments out only that dead read. The renderer visits
placements in descending order, selects them with the original wrapped yaw-byte
window, applies coordinate substitutions, then uses original signed-word screen
roll and palette indices.

The mountain oracle passes **6,817 comparisons**: 256 loader inputs, 25 explicit
variants, 1,536 yaw boundary cases and 5,000 random camera cases. It compares
144,112 vertices and 36,041 polygon commands with zero mismatches. All 132 source
instructions match after restoring the commented dead read. Original selector,
substitution, screen rotation and command consumption execute unchanged; line
clipping and pixel backends are intercepted and are outside this proof.

The native renderer fills each mountain's closed boundary, preserving concave
notches. Coverage regressions check an actual concave variant's notch, interior
and winding invariance, plus a synthetic flat road crossing the camera. Road
and side faces use native near clipping, triangles and depth buffering. Nearby
quads are subdivided before angular projection, which would otherwise join
their curved projected boundaries with a single chord and leave a road gap.
The closest three segments now use the recovered near-track visibility queue
and alternating yellow/brown edge highlights; see
[the near-track evidence guide](native-near-track.md). Matching gameplay,
projected geometry and ordered draw commands does not establish original pixel
equality. Playback remains silent.

Build the verification targets using the commands in
[the viewer guide](native-replay-viewer.md), then run:

```powershell
. .\scripts\env.ps1
& $AmigaPython tools/research/reference/verify_track_sides.py `
  --oracle build/windows-oracle/Release/track_sides_oracle.exe
& $AmigaPython tools/research/reference/verify_mountain_horizon.py `
  --oracle build/windows-oracle/Release/mountain_horizon_oracle.exe
& .\build\windows-oracle\Release\software_renderer_tests.exe `
  .\analysis\reference\vesuri\offline-extracted-payload.bin
& $AmigaPython scripts/check_native_rendering.py
```

The source verifiers save `reference-comparison.json`, `original-disassembly.asm`
and `oracle.json` in their respective ignored analysis directories. The mountain
verifier also saves `decoded-assets.json` with exact template/substitution ranges
and hashes. REA Evidence IDs are `ev_0a2f382c…` (side producer), `ev_1acc2d58…`
(left panel), `ev_b1823203…` (right panel), and `ev_842b03dd…` (mountains).
Complete records are in `analysis/rea/native-sides-mountains-evidence.json`;
`analysis/rea/native-sides-mountains-snapshot.json` retains the closed session.
The updated viewer check renders all 1,463 replay positions, verifies three
complete laps, and exports eight QA frames with separate visible road, side and
mountain coverage counts.

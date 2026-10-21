# Shared native scene commands

`app/replay_scene_commands.cpp` completes the three dense road passes with the
recovered far road painter, horizon and mountain allocation, and an optional
opponent car. Practice and competitive races use the same road path. The caller
supplies the dense `NearTrackFrame` and its resulting geometry; the builder
returns presentation commands and the history to retain for the next frame.

The builder is pure. It does not change gameplay RAM, source assets, its input
frame, or its input geometry. Normal frame capture retains the returned geometry.
Interpolated and alternate-camera queries use a private copy and discard that
history. Physics and its original update rate are unaffected.

## Original sequence

The extracted payload is 408,540 bytes, mapped at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Addresses below are runtime addresses; subtract `0xe700` for payload offsets.

The original caller at `0x65062..0x650e2` establishes the order:

1. Retain the dense queue boundary, then run far projection `0x65ec4` and far
   queue construction `0x67460`.
2. Clear the opponent marker at `0x65096` when `0x1ca22` is nonnegative. This is
   the Practice gate; it does not bypass either road builder.
3. Preserve the queue end while `0x67888` and `0x6953e` allocate the horizon and
   mountains in the same line arena as the road.
4. Run local opponent visibility `0x58600`, which selects relative-position zero,
   then far painter `0x690bc` and dense painter `0x68f28`. A car is admitted at
   its recovered painter slot. Its own negative-ahead and distance guards remain
   in `buildOpponentCarFrame`.

The result's `horizonMode` and `presentation.horizonMode` retain the edge visibility
mode at the original horizon call. Bit 7 controls the original eight-unit horizon
offset before roll projection. Capturing it before background allocation also
keeps that choice independent of the vertex scratch area those routines reuse.

Far edges, mountain edges and car edges share the `0x2328` admission limit. The
horizon allocation bypasses that guard. Mountain projection still updates vertex
history when no further edges fit. Those details affect later frames, so they
remain part of presentation history even in Practice. Dense viewport closure
also uses the final shared allocation while commands are painted.

The returned `denseLineBytes`, `farLineBytes`, `backgroundLineBytes` and `lineBytes`
describe consecutive allocation boundaries. `remainingOpponentOffset` and the
car flags expose admission outcomes for verification. Dense queue metadata is
preserved; all coarse-mesh coverage bits are set because the recovered painters
now supply the complete road command stream.

REA dossiers are retained locally under `analysis/rea/opponent-frame-schedule.json`
(`ev_2c473c4a84e78e4e55b2f34f103a98e6608e5e058c785e56621641ef85097cf8`),
`opponent-far-geometry.json`, `opponent-far-queue.json` and
`opponent-far-painter.json`. `analysis/scene-commands/proof.json` records their
evidence identities, the actual MC68000 caller instructions and source offsets.

## Verification

`replay_scene_commands_tests` executes the unchanged original instructions in
Musashi using fresh native asset/race initialization. Across all eight tracks
and both standard (`0`) and Super (`11`) contexts it checks:

- 1,920 cases: 384 Practice cases, admitted and rejected car cases, both horizon
  modes, different road drawing orders, and 384 exhausted-arena cases.
- Another 384 sequential Practice frames through the complete original dense,
  far and background caller at `0x64f80..0x650cc`. Projected vertex and endpoint
  history persists independently on each side between frames.
- 76,482 ordered commands, their colors and polygon/line boundaries; 276,480
  persistent vertices; final car markers and shared line allocation.

The run executes 56,080,636 original instructions with zero mismatches. The
oracle intercepts final fill/line raster primitives to observe commands; it does
not alter original code. Polygon edge order within one fill is normalized for
comparison. These are command and history checks, not a claim of pixel identity
between the Amiga rasterizer and the native software renderer. Views and car
guard fixtures are synthetic; driving traces are checked separately.

From the project root after configuring the oracle toolchain:

```powershell
cmake --build analysis/scene-commands/build --config Release --target replay_scene_commands_tests
& analysis/scene-commands/build/Release/replay_scene_commands_tests.exe analysis/assets/stunt-car-racer.scr-assets
```

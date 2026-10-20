# Headless driving replay

The replay harness targets the original main-loop gameplay interval
`0x5d47e..0x5d4ba` by default. Each tick supplies controls, runs the original game in
Musashi, and compares its declared gameplay outputs with independently evolved
native state. Track position, segment progress, physics inputs and timing state
must come from each implementation's own previous outputs.

This document describes the harness contract. A successful result is established
by a generated `analysis/driving-replay/check-results.json` with `status: passed`,
not by the presence of this document or by successful compilation.

## Run

From the project root:

```powershell
. .\scripts\env.ps1
& $AmigaPython .\scripts\check_driving_replay.py
```

The default capture is `analysis/captures/race`. Each checked-in scenario under
`tests/replays/` starts independently from that capture. Use `--capture` to select
another extracted, loaded local-practice snapshot and repeat `--inputs` to select
particular CSV files:

```powershell
& $AmigaPython .\scripts\check_driving_replay.py `
  --inputs .\tests\replays\accelerate-release-512.csv
```

`--skip-build` reuses an executable newer than its native sources and fixtures.
`--run-leaf-tests` also builds and runs the controller, track-progress, opponent
collision, driving-effects and race-timing oracles against the selected capture.
It records fresh executable, source, input and result hashes under
`analysis/outer-loop/`. Without this option, existing leaf results are retained
as historical evidence and are not presented as tests executed by this run.
`--native-only-verify` also builds and runs `driving_replay_native` for each
scenario, then compares all gameplay trace columns with the oracle trace.
That separate executable does not link Musashi or execute original CPU code.
`--verify-only` checks identities and input files without running a replay; its
report explicitly records that no comparisons ran. These options do not download
tools. Missing pinned Musashi sources are handled by the existing setup command
documented in the main README.

The underlying executable contract is:

```text
driving_replay_oracle chipram.bin inputs.csv trace.csv
driving_replay_native chipram.bin inputs.csv trace.csv
```

The oracle writes a JSON result to stdout, including `ticks` and `mismatches`.
The wrapper requires all eleven stage comparisons exactly once per requested tick,
the declared original interval, explicit byte-address projections and ordered
sample-audio register comparisons. It checks the trace's tick sequence, controls,
row count and instruction total. Its legacy `compared_bytes` counter includes
2,048 damage-strip occupancy comparisons per tick in addition to the projected
RAM bytes; the wrapper verifies that total against the declared projections.
It retains hashes of the executable, sources, inputs, trace and result.
Trace files and captured game data remain under ignored `analysis/` directories.

The native runner reports `cpu_emulation: false` and `comparison_performed: false`;
it independently evolves state and emits a trace, without an internal oracle.
The optional wrapper comparison excludes only `original_instructions`, which is
zero in the native trace. It checks the displayed gameplay columns, while the
oracle separately checks its broader RAM projections and effects at each stage.
Both executables currently use capture-RAM serialization adapters under `tests/`
to connect the recovered stages. Production calculations under `src/` use typed
native state. The standalone [native game](native-game.md) also uses this core,
with native asset loading, rendering, input and audio.

Draw Bridge track5 is admitted. Its animator runs after game-frame/wheel work
and before damage, at original return boundary`0x5d4a2`. The comparator checks
the phase, active latch, raw road geometry and eight cached vertex heights every
tick. Shared renderer scratch is covered separately by the isolated animator
oracle. Native physics, progress and restart data are refreshed from the native
update, while recorded render frames retain their own geometry. See
[Draw Bridge](draw-bridge.md) for the original preview's stationary pose and
the moving race behaviour.

## Controls

Input files are plain ASCII without a byte-order mark or quoted fields. They
contain exactly `tick,controls`, followed by contiguous zero-based ticks and
decimal masks, with one row per tick and LF or CRLF line endings. Add masks to
combine held controls.

| Mask | Control |
| ---: | --- |
| 1 | Accelerate |
| 2 | Brake/reverse |
| 4 | Left |
| 8 | Right |
| 16 | Fire/boost |
| 0 | All controls released |

These are external control inputs. They never specify position, velocity,
segment index or road geometry. The game retains its original acceleration
latch, so release does not necessarily mean coasting without engine force.
The scenarios are authored test inputs, not recordings of a human-driven lap.

## Original schedule and recovered state

The original iteration decrements the frame counter and calls player input,
physics, engine pitch, game-frame processing, drawbridge animation, speedometer,
damage/timers, frame throttling/laps, and distance display, in that order. It
performs one physics update per iteration. The additional catch-up loop in the
enhanced Vesuri source is absent from this disk.

Within frame processing, the recovered camera/grid/geometry stage derives the
next tick's segment, lateral road origin, forward progress and target heading
from the updated world coordinates. It includes diagonal neighbouring-cell
lookup, straight/diagonal/curved geometry, road boundaries and retained off-grid
progress. It also preserves the original one-unit nudge when an X or Z low word
is zero. The fixed-point arithmetic and original scratch aliases are intentional.

The frame throttle is derived from the original accumulator rather than supplied
as an authored control. Graphics-related functions can change gameplay state:
for example, wheel suspension rendering shifts the bounce accumulator, and
ground/edge effects consume the shared random generator. The composed comparison
must account for these effects even when pixels are outside its output contract.

## Evidence and boundaries

Before execution, the wrapper checks the supplied ADF hash, its documented
`0xdc00` payload range, the extracted payload hash, the capture manifest and RAM
hash, the source savestate hash, and the pinned Musashi sources. It runs the
controller, track-progress, opponent-collision, driving-effects and race-timing reference verifiers, checks
their original code/table spans against the capture, and records source hashes.
Generated track arrays and runtime state are checked as capture data; they are
not assumed identical to the disk image.

The original pixel routine changes eight opcode bits to select which bitplanes
it sets or clears. Capture checks allow only bit 6 at those eight documented
addresses and record the loaded bytes separately; every other code bit remains
subject to exact disk comparison. This is original game behavior, not an oracle
patch or a replacement renderer.

Existing leaf-oracle result files under `analysis/outer-loop/` are included with
their hashes and marked as retained results unless `--run-leaf-tests` reruns them.
Missing result files are explicitly identified; an existing failing
or incomplete result cannot silently become a passing replay report.

The interval ends before the outer pause, restart, race-end and buffer-swap
gates. Local Practice and local competitive races are admitted. The opponent
stage runs original AI, track following, distance and contact handling in their
original order; it preserves the off-grid branch that skips player contact.
Network games remain outside this replay. See [opponent driving](opponent-driving.md)
for independently evolved full race and natural collision comparisons.

The original CPU renderer executes with a headless hardware boundary that reports
completed blits. This does not reproduce blitter operations, pixels, DMA,
interrupt interleaving, audio waveforms or real frame timing. The executable's
result declares the gameplay projection and effects actually compared. Agreement
here establishes that interval under those conditions, not whole-frame RAM
identity or complete game fidelity.

The harness resets CPU registers and the test stack pointer at each interval entry.
Gameplay RAM persists across ticks independently on both sides. The original
keyboard/interrupt boundary is supplied by the test harness, and no interrupts
run during a tick. Ordered register writes from the recovered sample-audio helper
are compared; this does not establish complete Paula audio output or timing.

## Restart and race-end mode

Add `--full-loop` to continue through the original outer-loop gates to the
next `0x5d47e`, the race-end fire-button wait at `0x6488a`, or the quit fade at
`0x5d674`. This mode preserves the original CPU context between iterations.
The first entry still establishes the documented register/stack preconditions.
Both implementations keep their own state across restarts; only external
controls are supplied. No original output seeds the native simulation.

```powershell
& $AmigaPython .\scripts\check_driving_replay.py --full-loop `
  --run-leaf-tests --native-only-verify `
  --output .\analysis\driving-replay\full-check-results.json
```

The recovered restart includes unsafe-segment rejection, the full vehicle
reset, grid placement, geometry and physics, suspended-car placement, wheel
and debris initialization, two frame/timer warmups, both buffer swaps and engine
restart. Lap records and damage remain persistent. The original renderer uses
segment/camera fields as temporary cursors, so final restart comparisons cover
the declared persistent projection. The separate initializer oracle checks
seven intermediate boundaries before rendering can reuse those fields.

The original writes a six-frame buffer wait. The headless comparator explicitly
services that countdown at swaps and presentation delays. This is a deterministic
external wait boundary, not execution of the original interrupt handlers.
`frame_wait_vblanks` counts the supplied countdown steps; it is not measured
display timing. Original sample playback and audio-disable register writes
remain ordered and compared. Palette and busy-delay CPU instructions run during
restart. Their pixels, wall-clock duration and interrupt interleaving are excluded.

An optional input header `tick,controls,quit` adds a 0/1 quit-key value, sampled
at its original query. Quit requires `--full-loop`. A timeline must end on a
terminal transition; further input rows are rejected. For example:

```powershell
& $AmigaPython .\scripts\check_driving_replay.py --full-loop `
  --inputs .\tests\replays\transitions\quit-1.csv --native-only-verify
```

Trace columns now also contain `lap_counter`, `crossing_armed`, `race_complete`,
`outer_action`, `restarts` and `quit`. Actions are 0 continue, 1 restart,
2 race end, 3 quit, and 4 pause wait. The native transition module recovers pause
bookkeeping, but the full-loop runner currently requires an unpaused capture
and supplies no pause/unpause timeline. It stops at the end-message input wait
or quit fade; returning to the menu is outside this composed replay.

Use `--full-loop --require-complete-lap` with a suitable controls timeline to
require a complete lap, rather than accepting a short matching sequence.
The validator requires two observed crossings, all track segments in order,
the crossing-latch disarm and no restart during the candidate lap. It rejects
the initial counter-zero-to-one crossing as a complete-lap demonstration.
See [complete-lap evidence and timeline authoring](replay-lap-validation.md).
The four default short scenarios demonstrate restarts. A separate retained
controls-only lap regression checks a full cycle with no restarts:

```powershell
& $AmigaPython .\scripts\check_driving_replay.py --full-loop `
  --inputs .\tests\replays\laps\authored-lap-506.csv `
  --require-complete-lap --native-only-verify `
  --output .\analysis\driving-replay\complete-lap\check-results.json
```

It passes 506 original/native updates. Crossings at ticks 24 and 505 enclose
all 44 segment boundaries, with latch disarm at tick 272. These are authored
controls selected by a native feedback experiment, then checked against the
original. Its provenance accompanies the CSV; it is not a human recording or
an interrupt-timing test. The lap input lives in a subdirectory so the default
short-scenario run remains stable.

# Headless driving replay

The replay harness targets the original main-loop gameplay interval
`0x5d47e..0x5d4ba`. Each tick supplies controls, runs the original game in
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
The wrapper requires all nine stage comparisons exactly once per requested tick,
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
native state. This is a testable gameplay core, not yet a standalone game with
asset loading, a renderer or a platform input/audio loop.

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
gates. Local practice is the admitted replay mode. Opponent collision routines
have separate differential tests; autonomous opponent AI and network games are
outside this replay.

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

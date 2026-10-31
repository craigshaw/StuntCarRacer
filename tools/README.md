# Development tools

`SCR_BUILD_DEV_TOOLS` builds the Windows viewer and headless native replay runner
from `replay/`. They share production code and are not installed with the player.
Normal player controls are in [playing](../docs/playing.md).

## Viewers

Run these from the repository root:

| Mode | Launch | Inputs |
| --- | --- | --- |
| Fresh Practice | `.\scripts\start-native-race.ps1 -Track 0` | Local asset pack; no capture |
| Recorded replay | `.\scripts\start-native-replay.ps1` | Retained capture and authored controls |

The direct viewer starts paused. Drive with arrows/WASD, Space for boost, or
XInput stick/D-pad, triggers and A/RB. Acceleration is latched until braking.
Focus loss or controller disconnection pauses and requires explicit resume.

| Action | Direct driving | Recorded replay |
| --- | --- | --- |
| Pause / resume | P, Escape, controller Start | Space |
| Reset / seek | R or controller Back/View: fresh race | Left/Right: tick; Home/End: start/end; timeline: scrub |
| Smoothing | I | I |
| Wireframe | F2 | W |
| Audio | M toggles mute | Silent playback |

`-Track` accepts 0–7. Replay supports `-Paused -StartTick 272` and `-ExactTicks`.
The replay shortcut is `replay/Watch Native Replay.cmd`; original-game emulation
tools live separately in [research/](research/README.md).

## Static ghost rendering proof

After loading `scripts/env.ps1`, run `& $AmigaPython scripts/check_native_ghost.py`.
It builds `native_ghost_proof`, runs focused CTest checks and writes identified
results, comparison images and two animated previews under `analysis/ghost-proof/`.
`--skip-build` requires matching source/executable identities. Pillow is needed
only for QA conversion; the replay wrapper's bundled image runtime is reused.

The default inputs are the retained race-entry capture/payload, authored
`tests/replays/laps/authored-lap-506.csv` and identified retained original trace.
These are private development fixtures; the player still needs no capture.
CTest skips this proof if its private inputs are missing. The tool records the
native trajectory and samples that same path twelve updates ahead of the camera
to make the ghost visible; it is not a saved personal-best feature.

World-space car rendering, foreground composition, all four views, interpolation
and simulation/RNG isolation are exercised. Original colour spans and painter
order are retained; optional depth metadata follows the same clipped fills.
Every retained update and quarter-update in all apertures must have complete
painted-track depth coverage. `proportions.png` compares the calibrated exterior
with the recovered procedural opponent at matched footprints and distances;
the 3D model does not claim exact original silhouettes at every view angle.
Static boundary checks do not establish whole-feature production acceptance.
Player settings and durable ghost recording are not implemented.
See the [design and proof gates](../docs/components/personal-best-ghosts.md).

## Draw Bridge ghost proof

After loading `scripts/env.ps1`, run
`& $AmigaPython scripts/check_native_ghost_bridge.py`. The local native asset pack
is the only private input. The wrapper builds `native_ghost_bridge_proof`, runs
six focused CTest checks and retains source/asset/executable identities, synthetic
seeds, controls and native traces under `analysis/ghost-bridge-proof/`.
`--skip-build` requires matching source/executable identities. CTest skips the
headless proof when its asset pack is absent.

`matching.gif`, `mismatch.gif`, `same-shape.gif` and `comparison.png` show a native
league trajectory against compatible and incompatible live geometry. An observer
camera makes the coincident ghost visible without moving its recorded position.
Captions show local suppression and opacity; their conditions indicator is QA UI.
The real AI opponent affects recorded bridge holds, and live occupancy/release
continues normally. All four views, exact/half updates, landing, reverse/pause,
painted-depth coverage and full simulation/RNG isolation are checked.

The initial approach, opponent position and phase are authored through native
initializers/adapters before driving. Subsequent motion and bridge updates use
normal native simulation. This is a finite crossing proof, not an earned lap or
an original CPU comparison. Player lap recording, persistence and feedback remain
separate work. See the [bridge proof contract](../docs/components/personal-best-ghosts.md#draw-bridge-proof).

## Recordings and comparisons

`scripts/check_native_laps.py --verify-original` checks the career personal-best
recorder, Practice playback timeline, recovery timing and session isolation.
Its results and source/fixture/executable identities stay in
`analysis/lap-recording/`; `--skip-build` requires a matching receipt. It uses the
local asset pack and retained complete-race capture/controls/original trace.
The player recorder is in memory only at this stage. Durable ghost files and
player controls remain separate work; the development CSVs below are not that
future recording format.

The direct viewer writes controls/traces under ignored `analysis/live-driving/`.
Player recordings require `--recordings-dir`. The CSV uses contiguous zero-based
ticks and control bits 1 gas, 2 brake, 4 left, 8 right, 16 boost; optional
`quit,hold_acceleration` columns retain host actions and throttle policy.

Interactive recordings omit their exact race-entry seed. Original-code comparisons
require that seed and reject Modern held throttle; see
[verification prerequisites](../docs/development.md#original-code-and-replay-prerequisites).
Use `scripts/check_driving_replay.py --native-only-verify` for the retained
Original-control fixtures. Headless runners accept `chipram.bin inputs.csv trace.csv`.

[Everyday scripts](../scripts/README.md) and [development](../docs/development.md)
cover builds, checks and fixture setup.

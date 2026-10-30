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

## Recordings and comparisons

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

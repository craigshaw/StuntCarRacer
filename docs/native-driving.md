# Native keyboard and controller driving

Double-click `Drive Native.cmd`, or run:

```powershell
.\scripts\start-native-replay.ps1 -Live
```

The launcher checks local assets and verifies both replay and live simulation
against the retained original race before opening **Native driving**. It starts
paused at the same local Practice snapshot used by the verified replays. Click
**Drive**, press **P**, or press the controller's **Start** button to begin.
This uses the native C++ physics and renderer; the executable has no CPU emulator.
`Watch Native Replay.cmd` continues to open the authored replay.

| Action | Keyboard | Xbox / XInput controller |
| --- | --- | --- |
| Accelerate | Up or W | Right trigger, stick up or D-pad up |
| Brake / reverse | Down or S | Left trigger, stick down or D-pad down |
| Steer left / right | Left / Right or A / D | Left stick or D-pad left / right |
| Boost | Space | A or right shoulder |
| Pause / resume | P or Escape | Start |
| Reset to the starting capture | R or Reset button | Back / View |
| Smooth / exact tick presentation | I or Smooth button | — |
| Wireframe | F2 | — |
| Mute / unmute sound | M | — |

The original acceleration is **latched**: a gas press starts acceleration and
releasing it keeps accelerating. Use brake to slow or reverse. Controller axes
and triggers select the original digital input bits; analog steering strength
does not change the recovered physics. The left stick uses a 7,849-unit deadzone
and triggers activate above 30. Opposing steering directions cancel; brake takes
priority over gas when keyboard/controller inputs conflict.

The first connected XInput controller is selected automatically. The window
shows whether one is connected, supports hot plugging, and pauses if it
disconnects. Keyboard input remains available alongside the controller. Losing
window focus pauses driving and clears pending input; returning to the window
requires an explicit resume. A reset starts another recording and returns to
the captured Practice state. Automatic off-track recovery and race-end gates
still use the recovered native routines. Original engine samples and sound
effects play through native stereo audio. Pause and focus loss stop playback;
M toggles mute. Menus, track selection and autonomous opponent driving are
outside this live mode. See [native audio](native-audio.md) for recovery evidence
and the playback boundaries.

## Timing and interpolation

The same simulation step serves authored replay and live driving: controls,
physics, engine, track progress, near rendering state, ground/wheel/damage
effects, race timing and outer transitions. Physics advances only in fixed
**120 ms** steps (about **8.33 Hz**). A separate **60 Hz** presentation loop polls
controller state and draws interpolated camera/suspension views. Short keyboard
or controller pulses are retained until the next physics tick. Pause/focus loss
clears that pending input.

Live interpolation blends the previous and current completed states, adding one
physics tick of presentation latency. Restart and terminal events snap to their
new state. Press I for immediate exact-tick presentation. Stalls use bounded
catch-up with ordinary fixed steps; the timestep is never enlarged. The live
scene retains two presentation snapshots and its independently evolving native
state rather than accumulating a replay in memory.

## Recordings and verification

Every drive writes unique files under `analysis/live-driving/`:

- `drive-<timestamp>-controls.csv`: the actual mask and quit flag sampled on each
  physics tick, in the replay runner's input format.
- `drive-<timestamp>-native.csv`: the corresponding native gameplay trace.

Files flush after every tick. Closing or resetting preserves the previous
drive; a reset creates another pair. Native recordings are not automatically
claimed to match original execution. After a drive, compare its inputs with the
original using:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_driving_replay.py `
  --inputs analysis/live-driving/drive-<timestamp>-controls.csv `
  --full-loop --native-only-verify `
  --output analysis/live-driving/original-comparison/check-results.json
```

The standard `scripts/check_native_rendering.py` check includes 18 input/clock
regressions, the existing 14 interpolation regressions, and the complete authored
race through live mode. Live mode matches all **1,463 original gameplay ticks**
while rendering **10,534 presentation frames** on its fixed-step clock. The
recorded input file is checked for dropped or changed masks. Replay mode still
checks all exact tick images and 10,528 presentation samples.

Additional live scene checks cover the four earlier scenarios: **1,920 updates
and five automatic restarts**, with matching original traces. Evidence is in
`analysis/native-render/live-transitions/check-results.json`; the fresh original
reference is retained in its `reference/` subdirectory. These are deterministic
test inputs; actual keyboard/controller checks are recorded separately.

A 256-tick prefix observed from the live keyboard drive was copied while the
window was running and replayed through the original and independent native
runner. Its recorded live trace matches every declared gameplay column, with
zero mismatches. Evidence is in
`analysis/native-render/live-keyboard-prefix/check-results.json`. This bounded
check establishes that the interactive keyboard path reaches the same simulation;
it does not claim original comparison for every later live input.

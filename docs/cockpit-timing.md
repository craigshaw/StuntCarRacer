# Original cockpit call timing

The cockpit consumes ordered draw requests from the recovered gameplay routines.
The running clock cannot be read retrospectively after each update: a lap
crossing displays its completed time and then clears that same clock slot before
the scheduler returns. `RaceTimingEvent` therefore retains the time and the
meaningful low bytes of the original drawing arguments at the call itself.
This extension makes no change to gameplay state or arithmetic.

The original payload is the supplied ADF's slice at disk offset `0xdc00`, length
408,540, mapped at runtime `0xe700`. Its SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
The corresponding ADF hash is
`548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
Addresses below use that runtime mapping. The pinned enhanced source provides
names for navigation; its added multi-step scheduler, clock increment, throttle
gate, and lap-display hold timing differ from this disk.

## Clock, lap and result events

The local scheduler is `[0x5db34,0x5dc38)`. Its existing native implementation
advances the same packed-decimal clocks, crossing flags, records, winner state
and blink countdown as before. Events preserve the following original calls:

| Event | Original call path | Immutable payload |
| --- | --- | --- |
| `lapTimerHud` | `refreshLapTimerHUD` `0x5ded0` → `renderLapTimeDisplay` `0x5e326` | `time=times[0]`, `visibility=D0`, `displayPosition=D1=2`, `timeSlot=D2=0`; existing `value` remains the visibility byte |
| `resultsHud` | `handleLapCompletionTiming` `0x5dca6` → results icon `0x5e098` → time display `0x5e326` | `time=times[2]`, `visibility=D0=0x80`, `displayPosition=D1=3`, `timeSlot=D2=2`; existing `value` remains icon 7 or 11 |
| `lapCounterHud` | inline crossing code at `0x5dd80` | existing `value` is the new player lap counter; text is positioned at column 6, row 22, with offsets `(2,2)` before digit advance |
| `leaderIcon` | `renderPlayer1CarGraphic` `0x5e0d6` | existing `value` is the original `D2` icon selector, 7 or 11 |

The timer argument fields apply to the two time-display events; counter and
leader requests retain their complete values in `value`. Irrelevant scratch
registers are not presentation inputs.

An ordinary player clock increment redraws the running time only when its
packed-decimal hundredths addition carries into seconds, the lap counter is
nonzero, and no completed-lap display is being held. That draw sets `D0=0`, so
the hundredths digits are blank. The original scheduler adds packed `0x13`
on each update; it does not use the enhanced source's throttled `0x14` route.

At a player's first line crossing, the lap counter changes to 1 and is drawn;
the clock is cleared for the start of the first timed lap. At a later crossing,
the original fractional line-crossing correction runs first. Records are
updated, the completed-lap hold is set to `0x1b`, and its clock is drawn with
hundredths visible (`D0=0x80`). The counter is drawn next. The candidate best lap
can then replace slot 2, and the running clock slot is cleared. Its earlier draw
request must retain the completed time.

Each subsequent scheduler call decrements that 27-update hold exactly once.
The 27th replaces the held lap with the results icon and slot-2 time. An
opponent lap that replaces the best slot can clear the hold and issue the
results draw immediately. Finish detection evaluates the opponent before the
player, emits the leader icon once, and then updates the existing message and
outcome state. Event order is the original execution order.

## Startup, restart and main-loop order

The original `runMainGame` startup performs these cockpit operations:

1. Copy the main-game background and palette; select foreground 14/background 0.
2. Set up damage (`0x5d384` → `0x5d87e`).
3. Draw static `L` and `B` labels (`0x5d3b0` → `0x5e474`).
4. Draw the leader and result car icons with `D2=11`
   (`0x5d3ba` → `0x5e0d6`, then `0x5d3c0` → `0x5e0aa`).
5. Initialize the player and render the first warm-up frame, then call the
   timing scheduler; decrement `frameCounter` and exchange display buffers.
6. Advance physics, render the second warm-up frame and call timing again;
   decrement `frameCounter`, update the speed bar, and exchange buffers.
7. Enable frame processing/display updates, perform the original fade/wait,
   and enter the loop.

There is no unconditional startup call to the live lap timer, lap counter,
fuel digits, result time or distance display. The copied background supplies
their initial appearance. All eight ordinary fresh track seeds have lap
counter 0 after the two warm-ups: the player is on `lapLineSegment-1`, clock
slot 0 is packed `09:00.26`, and the global timing counter is 2. Neither warm-up
generates a timer event in those normal starts. This is why drawing a synthesized
`0:00.00` or lap 0 at startup changes the original appearance.

The original player restart initializer `[0x605b6,0x6076c)` itself has no
cockpit/timing calls. The caller's normal warm-up frame sequence can still
produce timing requests because a restart retains the race's clocks and lap
state. Those requests must be kept rather than reconstructing them from the
final state. The second warm-up's speed-bar update remains in its original
position.

Each normal local update starts at `0x5d47e` and runs:

| Order | Routine |
| --- | --- |
| 1 | decrement `frameCounter` (`0x1bbac`) |
| 2 | player input/boost (`0x5d8a2`) |
| 3 | physics (`0x6185c`) |
| 4 | engine pitch (`0x5b56c`) |
| 5 | track/car rendering and wheel suspension placement (`0x64e4c`) |
| 6 | Draw Bridge animation (`0x5a794`) |
| 7 | speed bar (`0x5e23e`) |
| 8 | damage and timers (`0x5dfb4`) |
| 9 | race clocks, crossing, records and finish (`0x5db34`) |
| 10 | distance and, if unfinished, leader icon (`0x5e12a`) |
| 11 | race-end handling and buffer exchange |

The distance display is gated by `(frameCounter & 3)==0`, not by the global
timing counter or the 60 Hz presentation clock. It draws its leader icon after
the digits only while race-completion state is zero. Race completion itself
can draw the final leader icon in the preceding timing stage.

Boost digit drawing occurs inside player input only when an eligible boost
consumption decrements the fuel timer past zero. The original call at
`0x608fa` enters `updateBoostFuelDisplay` `0x60824` with packed delta `0x99`,
updates/clamps the packed fuel byte, and draws the resulting digits. A held
boost button is not an instruction to redraw every presentation frame.
The controls API already emits `fuelHudRedraw` and `displayedFuel` at exactly
that point. Configuration calculates the initial packed fuel inline and does
not call this draw routine. The original payload has one absolute JSR to
`0x60824`, at `0x608fa`, and no absolute JMP to it.

Wheel rotation is a separate PAL vertical-blank operation. It is admitted when
the display-update flag is negative and the wheel-update-disable byte is zero;
it is not advanced by a cockpit draw, a clock carry or an interpolated frame.
The native wheel module recovers that interrupt path independently. Wheel
suspension placement still belongs to the normal game-frame stage.

## Original-code verification

`tests/cockpit_timing_oracle.cpp` executes the original scheduler and original
text/icon routines in Musashi MC68000 mode. Instruction hooks observe call
boundaries without patching instructions or substituting calls. At
`0x5e326`, the oracle reads the actual low `D0/D1/D2` bytes and snapshots the
selected clock bytes before their later writes. It compares those payloads,
ordered event kinds/values, and the existing gameplay memory projection
against independently advanced native state. Rendered pixels and font/cursor
scratch remain excluded here; the cockpit drawing oracle checks pixels.

The first run against the fresh track-0 seed passed **15,997 cases** and
**25,033,799 original instructions**, with zero mismatches. It observed:

- 2,551 timer requests, including 1,246 with hidden and 1,305 with visible
  hundredths, and 1,309 snapshots that differ from the later cleared clock slot;
- 253 result requests, 1,567 lap-counter requests and 1,634 leader-icon requests;
- every fraction byte, four carry deltas, both player clocks and all display
  gates; minute/second carry boundaries; both cars' crossing phases, six lap
  counts and both local game modes;
- the explicit 28-call completed-lap sequence (crossing plus its 27 subsequent
  updates), 4,096 independently evolving calls and 4,096 random state cases.

The previous race-timing regression also passed **144,049 cases** after the
event extension. Runtime state and original arithmetic are unchanged.

`tests/native_cockpit_replay_oracle.cpp` additionally executes the complete
original game loop, including its actual cockpit drawing routines, against an
independently evolving `ReplayScene`. It compares both original buffers over
the full 320 × 32 instrument strip at rows 168–199, including HUD changes
inside a restart's warm-up frames. At the original buffer-wait boundary it
preserves the interrupted CPU registers and stack, executes the original
`0x616dc` vertical-blank prefix until the actual wait counter reaches zero,
and compares the native wheel accumulator, phase and Copper sprite pointers.
The first six interrupt states form the retained rendering timeline; additional
restart waits still advance persistent state. The actual pending
`0x69f04` sprite-header update is then executed and all twelve wheel sprite
placements are compared with the immutable native snapshot.

Fresh current startup seeds also have both initial instrument strips checked
against untouched original startup drawing before replay begins. Older seeds
whose text masks are uninitialized can be reconstructed with those original
calls for regression use. The complete-lap capture explicitly preserves its
existing HUD instead; it does not receive a new startup screen.

Initial full-loop runs passed 4,096 competitive updates over all eight tracks,
including 17 restarts, plus 3,116 updates comprising the original-verified
1,463-update three-lap player race, the 1,482-update opponent finish and a
171-update natural car-contact run. The three-lap race issued 180 timer draws,
three result draws and four lap-counter draws. A further 4,096 fresh Practice
updates passed on all eight tracks. These are real input histories; no midrace
clock, wheel, lap or opponent state is injected.

The reproducible `scripts/check_native_cockpit.py` wrapper builds the leaves
and full-loop oracle, creates fresh Practice and competitive starts, validates
the retained complete-race fixture's provenance, and checks the corresponding
gameplay traces against the original. Its manifest has tab-separated columns
`name`, `seed`, `controls`, `maximum`, and `preserve_hud`. The last column is 1
only when the seed's existing HUD must be retained. For example:

```powershell
analysis/cockpit-replay/build/Release/native_cockpit_replay_oracle.exe `
  --manifest analysis/native-cockpit/cockpit-scenarios.tsv `
  analysis/reference/vesuri/offline-extracted-payload.bin
```

Example command after building the `cockpit_timing_oracle` verification target:

```powershell
analysis/cockpit-timing/build/Release/cockpit_timing_oracle.exe `
  analysis/reference/vesuri/offline-extracted-payload.bin `
  track-0 analysis/native-game/startup/track-0/native-initial.bin
```

The local receipt is `analysis/cockpit-timing/report.json`; it includes payload,
seed, source and executable hashes. RAM, original assets and receipts remain
ignored outputs.

REA dossiers are retained locally in `analysis/rea/`: timer refresh
`ev_194c86082b39271059838db6dc9aef5d523468abef2687e7fd2a5e47c335a82a`,
completion `ev_dc66b671ce449be0cebc699b4d2b27157ef907d61e5b2dc88b2b1140f6513729`,
crossing `ev_f32a0e9c96f30fdd5e081b00d88bf6dc8fbc403d93de30f1b3d81990abedae49`,
leader `ev_ce8736b1d42b1da21f0cd5ac1ba4d78ea2c4a4ed70894077fd28aec84e4a74b2`,
and time drawing
`ev_4ffb8b244eb51312c66b7f14b8902014fb4ca0fd6d10af654abbf72a8273051f`.
Capstone MC68000 mode independently cross-checks the caller instructions,
register arguments and startup/main-loop schedule.

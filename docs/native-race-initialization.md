# Fresh native race initialization

The runtime starts with zeroed 512 KiB memory and the original payload from the
local asset pack. No RAM capture, ROM, emulated CPU, generated memory patch or
embedded original asset array supplies initial game state.

The native initialization order is:

1. Set up recovered graphics and samples.
2. Reproduce the original successful-load memory transition with
   `initializeNativeLoadState`.
3. Run `initializeNativeGameData`, corresponding to original
   `initializeGameData` at `0x5a9b6` and `initializeGameTables` at `0x5aa38`.
4. Decode the selected track into its native runtime tables.
5. Run `initializeNativeRace`: clear the menu text displacement as the original
   overview return does (`0x5d31c`), reset player configuration (`0x5d726`), establish
   the local default car, race colors and spawn, then run the separately verified
   vehicle restart closure, including its two warm-up frames and engine request.

Original configuration bytes are copied from the locally loaded payload table
at `0x1fe6c`; fuel comes from the selected track. Damage, laps, input, timers,
vehicle motion and suspension are reset by recovered routines, not by copying
an observed Practice state. Random seeds come from the original stored seed
data. Native startup chooses the local default car and Practice track as direct
menu choices.

Two details differ from assumptions one might make from the enhanced reference
source. The original seed reset reads an overlapping saved byte at `0x636b9`.
Also, the zero-fuel ABCD loop executes 256 times and its first byte underflow sets
the extend flag, producing BCD `0x57`. The early game-data reset takes that path
before a track's fuel value is loaded. Both behaviors are preserved.

The successful-load path at `0x5c960` loads its validation value from the disk
immediate at `0x5c962`. After hardware cleanup it stores that value at `0x64aec`
at `0x5cefa`, updates the saved-data checksum state and marks records loaded.
Native startup reproduces those memory writes while omitting Amiga hardware
and exception-vector installation. The steering routine's original validation
check stays intact. Raw disk bytes at `0x64aec` are unrelated to the loaded
value; leaving them there silently disables yaw acceleration.

The previous fresh-start comparisons passed with that incomplete state because
both original and native steering were disabled. A behavioral regression now
drives a fresh car until it has wheel contact and speed, branches into left,
right and neutral controls, then requires opposite yaw changes. It failed
before restoring the load transition and passes after the fix.

## Verification

`native_race_initialization_tests` links Musashi only in the test executable. It
executes unmodified original instructions from the locally extracted payload
against independently evolving native RAM. It tests player configuration for
both configuration contexts, local/network slot clearing, eight fuel byte cases,
and random initial mutable fields. It also compares the game-data and standings
resets and 32 successful-load cases with varied checksum state. The 224 cases
compare 117,440,384 bytes with zero mismatches. All 512 KiB are compared except
the four exception-vector bytes at `0x24` for the load cases. CIA and custom-chip
writes are outside this game-memory check.

Run after building the optional oracle target:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/reference/verify_native_race_initialization.py
```

This writes hashes, original address/file ranges, instruction listings and test
results under ignored `analysis/native-race-init/`. Payload SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Its ADF offset is `0xdc00`, load address `0xe700` and size 408540 bytes.

REA evidence is retained in `analysis/rea/native-startup-evidence.json`:

- Player configuration: `ev_b7010b6c3c50fe609fd2b1deae9872fc025ce0cad13bb47a1e01e78334b6efdd`.
- Game tables: `ev_824d53d94843eafdd00b23ab8f4c2aa9ce118021eb0f453f8543083707d39550`.
- Main game: `ev_27a050ab837877ea1f251d783417358d3056d617eeb0e289c9db81795e48b0be`.

Ghidra did not include the final segment-flag loop in its player-configuration
body; complete MC68000 decoding and CPU execution cover through `0x5d87e`.

The load transition evidence is retained in
`analysis/rea/steering-startup-evidence.json`: incoming references to `0x64aec`
(`ev_6dde7f90f30cc08e0ba343abd1024d3d83da6a31586827f337c5e70b609e69ff`),
startup producer (`ev_207fae22b1202bd46e7484a29e8700b6c1529f18d1e97525f4cf1af2f8e464db`)
and memory stores (`ev_4f8bb380537dba6e2b6ef448229571cbb3c3b2797466810fe13dd686c4eab298`).

Fresh native startup states for all eight tracks
also pass 512 updates each against the original full main-loop interval. That
is 4,096 updates, including the moving Draw Bridge, automatic
restarts and ordered sample register writes. The original and native
states evolve independently from each fresh seed; no original results reseed
native state. Local results are retained under
`analysis/native-startup/integration/`, including
`original-filtered-results.json` with seed, controls, trace and executable hashes.

Fresh startup exposed two renderer-only bytes in an overly broad suspension
comparison range: `0x1bb7c` and `0x1bb7f`. Neither is read or written by physics.
The original renderer updates them between ticks, while the native renderer
owns separate presentation state. Their reader/writer addresses are recorded
next to the narrowed comparison in `tests/driving_replay_fixture.h`. Gameplay
checks retain every other previously declared byte; no startup constants were
added to make renderer scratch match.

## Scope

This starts local Practice or a local competitive race. Recovered
`initializeTrackCoordinates` constructs the original opponent look-ahead
geometry before the startup warm-up frames, retaining its random-stream
advance in both modes. [Native local league races](native-opponent-races.md)
describes the competitive menu, schedule and results flow. The full race closure uses previously verified
restart, physics, effects and timing functions; the reset oracle above proves
the reset routines, not equivalence of every GUI startup instruction.

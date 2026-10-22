# Native opponent driving

The native opponent follows the supplied disk's algorithms. The normal frame
schedule is player track progress, opponent logic, player/opponent distance,
opponent contact and lateral positioning, then opponent track geometry. The
off-grid branch omits distance/contact and still advances the opponent and its
geometry. Practice skips the complete opponent block. A race-start or recovery
gate inside the opponent logic preserves its state while that gate is active.

`src/opponent_driving.cpp` recovers the original wheel forces, suspension limits,
wheel-based car footprint, movement patterns, speed selection, obstacle avoidance,
acceleration and segment integration. `setupOpponentTrackGeometry` constructs
the camera-relative road coordinates and support heights used by both driving
and the procedural opponent renderer. `initializeOpponentCoordinates` recovers
the startup geometry call and one random height jitter shared by all four
initial wheel-position words.

All geometry, movement patterns, driver traits, damping values and distance
factors are read from the local asset pack. No original asset arrays are present
in these sources. The core uses typed state; only the fixture adapter translates
the original address representation. Musashi belongs to verification targets.

## Details preserved from this disk

- Integration uses the original multiplier 238/256, including each word wrap
  and intermediate arithmetic shift. The enhanced Vesuri source changes the
  scheduling and must not supply timing constants.
- Contact positioning already recovered in `src/opponent_collision.cpp` moves
  laterally by nine units per call. The enhanced source changes this to one.
- The second indexed displacement read in the geometry interpolation uses
  index 2 from address 0x1bbbe, so it reads the action timer at 0x1bbc0. It does
  not read the adjacent second offset byte at 0x1bbbf. The native code retains
  that actual indexed access.
- AI pattern selection and the wheelie trait use the game's shared five-byte
  random sequence, with no replacement generator and no additional calls.
- The word-sized negative minimum and signed table index are retained. The
  local pack supplies the complete 4096-byte window addressable by the
  distance factor lookup after its arithmetic shift.
- Geometry, wheel suspension and bridge animation evolve independently in the
  native and reference runs; no per-tick original outputs seed native state.

## Evidence ledger

The payload is the 408540-byte source image loaded at 0xe700, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
REA's m68k/Ghidra dossiers were cross-checked against local Capstone MC68000
disassembly and actual Musashi 68000 execution of unchanged payload instructions.

| Question | Original entry | Evidence | Result |
| --- | --- | --- | --- |
| Driving, speed and wheel dynamics | 0x63996 | `ev_7c1cfe0c6b89b0b9ab3576d7bb86ea985051a69e138c8f6180d68805b7c70651` | Recovered and CPU compared |
| Movement pattern and RNG gates | 0x5bd1e | `ev_e2c2b5d964006f5a01989c3dc37038b056500696310cb45d0152cb5c9ed970c2` | Recovered and CPU compared |
| Opponent road coordinates and bounds | 0x5a186 | `ev_ca2d6f264906e8700d925aa9d5b5331891570f5236430727f72add4573909602` | Recovered and CPU compared |
| Frame ordering and Practice gate | 0x64e4c | `ev_2c473c4a84e78e4e55b2f34f103a98e6608e5e058c785e56621641ef85097cf8` | Observed in original instructions |
| Existing collision force consumer | 0x63e2e | `ev_1eeef2c9e0555b951d4e5702873800c8e4dc7a5d2d215e2ae4a944edf90c6bc2` | Reuses verified suspension effects |

The retained dossiers are in ignored `analysis/rea/opponent-*.json`.

## Reproduction and scope

Configure with `SCR_BUILD_ORACLE=ON` and the pinned `MUSASHI_DIR`, build
`opponent_driving_oracle`, then pass the local `.scr-assets` path. The oracle
compares every byte of both 512 KiB memories after each call, rather than only
selected visible outputs. Its deterministic coverage comprises all eight
tracks; every segment at eight boundary/interior fractions and four camera
orientations; initialization;2048 independently evolving updates per track;
start/recovery gates; and 16384 scalar edge cases including all 32 trait-table
indices, network gates, throttle flags, patterns, signed speeds and suspension
limits. The Draw Bridge sequence also advances the original and native bridge
animators after geometry, matching the outer frame ordering.

The recorded result in `analysis/opponent-driving/report.json` is 65472 calls,
34326183936 compared bytes and 40488383 original instructions, with zero
mismatches. The evolving sequences complete three to five opponent segment
wraps on each track and exercise both airborne suspension and AI pattern
actions. These are deterministic component sequences with fixed camera/player
inputs; full race rendering, menu startup and player contact are checked by
their separate integration oracles. This does not claim network play support
or cycle-exact Amiga interrupt timing.

`scripts/check_native_opponents.py` also checks the complete native race loop
against the original. Its eight fresh competitive track routes use an authored
pre-season division placement for the local driver; all subsequent opponent
state comes from the recovered startup and normal gameplay. An idle-player
scenario ends naturally after the opponent completes three laps. An additional
171-tick control sequence (accelerate/boost, with left held on ticks 110–133)
produces seven car-contact ticks, exercising the collision producer, next-frame
force consumer and ordered sound writes without injecting midrace state.
Together these ten sequences cover 5749 original-rate updates. Live and retained
replay traces match the original, and exact frames, interpolation, immutable
snapshots and reverse seeking are checked independently of physics.

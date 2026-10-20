# Original Draw Bridge animation

The native animator is a typed C++ port of the unmodified original routine at
`0x5a794..0x5a996`. It reads the angle and opponent-height tables from the local
asset pack. No original table bytes or generated bridge geometry are embedded
in the source repository.

The source is the supplied 901,120-byte ADF with SHA-256
`548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
Its plaintext payload begins at disk offset `0xdc00`, is 408,540 bytes long, and
loads at `0xe700`; payload SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
The animator's payload offset is `0x4c094` (ADF offset `0x59c94`).
REA/Ghidra evidence is retained locally in
`analysis/rea/draw-bridge-animator.json`, evidence ID
`ev_fcafa8e4d3cfa7e9e8f1dd47bd09083f787c09a2e3d0c84f6e4b189d5dbc7cfc`.
Independent Capstone MC68000 decoding and an execution oracle cross-check the
original instructions. The enhanced Vesuri source changes this routine's
frequency, waveform and active-car handling, so it is not the behavioral oracle.

## Behavior and data ownership

`src/draw_bridge.h` separates passive source data, current vehicle/track inputs,
and mutable bridge state. `tests/draw_bridge_fixture.h` is the address-space
serialization adapter shared with the native driving implementation. Its
`scr::test::drawBridge::animate(memory)` entry point returns whether the raw
geometry and already-generated render cache changed.

Only track ID 5 changes. Otherwise, the routine is a no-op. When either car is
on segments `0x33..0x37`, the original active latch holds the bridge's current
shape and phase. An already-active latch is also retained while the opponent
is on approach segments `0x30..0x32`. This branch continues to update three
opponent guidance heights from the original table using `(phase+12)&31`.

With no car holding the bridge, the phase byte increments unless the sign bit
of the original frame-throttle flag is set. It wraps as an 8-bit value; the
waveform uses its low 5 bits. Subtracting 16 and complementing negative results
produces the sequence `15..0,0..15`, including the repeated extrema. The vertical
step is `(waveform+4)*32`, with original 16-bit arithmetic. Two ramp angle bytes
come from the original 16-entry table at `0x5a996`.

The geometry descriptor at table entry `0x1f060` resolves to a 72-byte raw geometry
region at `0x1f785` in this disk. Each update constructs 15 height increments,
duplicates the centre join, mirrors the leaf vertices, and retains the original
vertex flag at descriptor offset `0x20`. Physics samples this updated raw data;
the native renderer consumes the corresponding updated cache heights.

The original updates eight cached vertex heights through the segment 51 pointer
at `0x7aae6`, adding segment bases `0x1c6b6` and `0x1c6b8` with word wrapping. Its
first cached height deliberately combines descriptor bytes `0x10` and `0x13`;
this asymmetric selection is preserved. The update flag at `0x1bb71` skips these
cache writes on the initial call, then becomes `0x80`. Initial startup therefore
animates the raw data before rebuilding the cache, matching the original order.

The original outer driving loop calls the animator at `0x5d49c`, after the
game-frame presentation and wheel update, and before damage handling. The new
shape affects the next driving frame. Driving updates follow that order and
the original flag controls phase advancement. Presentation interpolation never
advances bridge simulation independently.

The original preview initializes the bridge once before building its road
geometry. While it waits for selection, rotation redraws that initialized shape
without advancing the bridge phase. Native preview preserves that behavior.

## Differential verification

`tests/draw_bridge_oracle.cpp` executes the original disk instructions unchanged
under a 68000 verification CPU and compares independently evolved native state.
The standalone run passes 263,359 calls and 27,650,265 compared bytes with zero
differences, executing 81,212,618 original instructions. It covers 131,072 phase and
boundary/flag combinations plus 131,072 cases visiting every possible segment
byte for either car at every phase. Both latch states, throttle sign,
initial/full cache writes, randomized signed height bases and byte/word wrapping
are checked. Arbitrary nonbridge track IDs remain no-ops. Further runs cover
26 complete animation cycles, sustained car occupancy and release without copying
original outputs back into native state.

The oracle restricts writes to the original output bytes and private verification
stack, compares all bridge outputs after every call, and compares the complete
512 KiB memory buffers at the end. The CPU is linked exclusively into this test;
the game runs the native C++ routine.

The retained local report is `analysis/draw-bridge/check-results.json`, with
original-code, executable and native-source hashes. Its raw disassembly and
oracle output live beside it.

## Native driving, rendering and replay

Choose **Practise → Tracks in DIVISION 1 → Practise The DRAW BRIDGE** in
`Drive Native.cmd`. The direct development launch also accepts
`scripts/start-native-race.ps1 -Track 5`.

The outer loop runs `bridgeStage` after wheels and before damage. It refreshes
the native physics, progress and restart track data after every bridge update.
The driving renderer captures its road data before that update, matching the
original call order. Moving track frames own their encoded surface data,
coarse road mesh and sidewalls. Later simulation, interpolation and replay
seeks therefore cannot change an earlier bridge pose. Camera interpolation
remains optional; bridge geometry advances at the original discrete tick rate.

Run the combined checks with:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_game.py --verify-original
```

The source-identified report is
`analysis/native-game/original-check-results.json`. It covers both menu/race
routes on all eight tracks, 4,096 fresh-start updates, and a separate 512-update
bridge approach. The original full-loop comparisons cover 5,128 updates in
total with zero mismatches. The preview checks compare 97 complete images
(6,208,000 indexed pixels), including every bridge view and repeated rotations.

The bridge approach deliberately uses the recovered restart initializer to
place the car at segment 49, then supplies normal acceleration for 512 updates.
It is an authored synthetic placement, not a captured or completed lap. The car
enters the bridge at tick 129 and leaves at 208; geometry and phase stay fixed
for 79 occupied updates and resume afterward. Raw geometry, cached mesh heights
and projected near-surface heights each change 376 times across the run.
The complete original CPU loop independently reproduces this drive, including
physics, damage, sound requests, a restart and every bridge update.

Live and retained replay images agree. A further 2,560 interpolation queries and
370 reverse/repeated seek checks preserve the recorded simulation and bridge
poses. The existing 1,463-update, three-lap regression still passes independently
through `scripts/check_native_rendering.py`. These finite checks establish the
recovered animation and integration; they do not claim whole-frame pixel
identity for the in-race renderer or autonomous opponent coverage.

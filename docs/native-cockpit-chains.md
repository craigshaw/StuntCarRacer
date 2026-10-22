# Native hanging-car chains

`app/native_cockpit_chains.*` recovers the hanging-car chains drawn at race
start and after a restart. The original frame calls `0x662b4` at `0x65102`,
after ground/edge particles and before cockpit top objects 13 and 14.
Masked asset IDs 19 through 22 supply the two alternating chain links on each
side; their bitmap bytes remain in the local asset pack.

## Original state and ordering

The original reads race-start timer `0x1bbdf`, chain position `0x1bbea` and
lift velocity `0x1bbe9`. A nonzero timer holds the chains. With a zero timer,
position exactly `0x60` stops drawing; otherwise byte subtraction moves the
position by velocity and byte addition increases velocity by eight. Both
operations wrap. The enhanced upstream source uses a less-than-or-equal test
and a different velocity increment, so its animation is not the disk contract.

For each link pair, the disk sets upper y to position minus 48 and lower y
eight pixels below it. Signed word comparisons admit rows at least 16. It draws
lower objects 20 then 22, followed by upper objects 19 then 21 when admitted,
then subtracts 16 from the byte position and repeats. The restart initializer
supplies position `0xb0` and velocity 8. After releasing the car, the ordinary
sequence is 176, 168, 152, 128, 96, followed by no further chain drawing.

`NativeChainState` is presentation state, advanced once per original rendered
frame including restart warm-ups. Callers reset it from the restart state
before the first warm-up, retain only the final warm-up draw list and store
that ordered list with the displayed frame. Composition uses those immutable
placements; interpolation or a repeated view never advances the animation.
The module does not write gameplay RAM, consume RNG or change timing.

## Evidence and verification

The source payload is 408,540 bytes, disk offset `0xdc00`, mapped at `0xe700`,
SHA-256 `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Capstone MC68000 decoding confirms original `0x662b4..0x66348`, including the
exact-equality branch at `0x662c8` and immediate eight at `0x662d8`.
REA/Ghidra evidence is
`ev_5b32a9823c67b22c04e91df6bddfa753974c8e7fd972bfcec8ef6748356d914d`;
whole-frame caller evidence is
`ev_e821ec28d2e99b52561d425b91f2789dbe5f41d170fec1c888eb183111e3595a`.

`native_cockpit_chains_tests` executes the unmodified original routine in the
test-only Musashi MC68000 CPU. State/draw checks cover all 65,536 position and
velocity byte combinations, every nonzero timer, signed drawing boundaries and
consecutive hanging/release sequences. These cases observe the ordered masked
object calls. Pixel cases separately execute the unchanged original masked
painter and compare every 320×200 pixel in both target buffers, including
unchanged surroundings. They cover positions 0 through 231; higher synthetic
positions are checked as commands because the original unchecked painter can
address beyond the canvas. Normal hanging/release positions are inside this
pixel-checked range. Tests also verify immutable retained draws and unchanged
source RAM during native presentation.

Build the optional oracle target and pass the local payload:

```powershell
cmake --build build/windows-oracle --config Release --target native_cockpit_chains_tests
build/windows-oracle/Release/native_cockpit_chains_tests.exe analysis/reference/vesuri/offline-extracted-payload.bin
```

These checks establish chain state, draw ordering and masked pixels. They do
not establish custom-chip timing or universal world-raster identity.

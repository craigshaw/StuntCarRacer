# Native race messages

`app/presentation/native_race_messages.*` restores the original in-race message admission
gate around the already verified `drawNativeGameMessage` glyph renderer.
Message inputs belong to the rendered tick. Retained and interpolated views
read `NativeRaceMessageFrame`, rather than later gameplay RAM.

The original normal frame calls `0x5e508` at `0x651b0`, after ground/edge effects,
chain animation, wheels and flames, and before the later damage/timing stage.
That order matters: the snapshot reflects the current original frame, while
later damage/timing stages can change the selected message, countdown and blink
colour. Presentation captures the message after those drawing effects and
before timing advances.

## Recovered contract

The gate at `0x5e508..0x5e540` draws only when all three conditions hold:

- `0x1bb8e` (active) has bit 7 set.
- `0x1bb6c` (countdown) is zero or a signed byte at least three, or the
  message index at `0x5e65b` is `0x3c` (PRESS FIRE).
- `0x1bba4` (pause state) has bit 7 clear.

The renderer consumes the group count at `0x5e65a`; DROP START is index `0x2c`,
PRESS FIRE is `0x3c`, and both use four groups. `0x1bb88` selects black glyphs
when zero and white glyphs otherwise. The message table and initialized primary
font remain in the local asset memory. No strings or bitmap glyphs are copied
into source control. The existing [message glyph comparison](native-menu-rendering.md)
establishes the two-line layout and three-character group spacing.

Normal frames draw transparent glyphs directly over the world. Original pause,
race-end and control-definition callers separately restore masked object 51
through `0x5fae8` before drawing their message. Its original table record at
`0x6a49c` places a 64×28 panel at `(128,27)`. `restorePanel` preserves that
distinction; the normal race path does not introduce a panel absent from the
original frame sequence.

## Evidence and verification

The original payload is 408,540 bytes, disk offset `0xdc00`, loaded at `0xe700`,
SHA-256 `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Capstone MC68000 decoding confirms the gate, table references, frame call,
panel helper and masked-object parameter decoding. REA/Ghidra evidence
`ev_af557ecbc31490b5c3d374ae37bfdeb40848236d23d9f5de7745b0b2ef86e87b`
independently corroborates the gate. Enhanced upstream labels are discovery
hints; the disk bytes and original execution establish these behaviours.

`native_race_messages_tests` executes the unmodified original code through the
test-only MC68000 Musashi oracle. It compares every countdown byte across both
signed active/pause branches, both normal and PRESS FIRE admission, normal
text and panel restoration, all current race message types, both buffers and
zero/nonzero blink bytes. Full-frame pixel checks include unchanged pixels
outside the message. Further checks retain a message snapshot while later
gameplay changes its active flag, index and panel position, and verify that
the native renderer leaves source RAM unchanged.

```powershell
cmake --preset windows-oracle
cmake --build build/windows-oracle --config Release `
  --target native_race_messages_tests --parallel 4
build/windows-oracle/Release/native_race_messages_tests.exe `
  analysis/reference/vesuri/offline-extracted-payload.bin
```

These checks establish message admission and bitmap composition. They do not
establish custom-chip timing or whole-world pixel identity. The game links no
CPU emulator and message presentation never changes gameplay, timing or RNG.

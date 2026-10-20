# Hall of Fame record state

`prepareNativeHallOfFame` retains the logical record preparation performed by
the original screen at `0x5fbc6..0x5fc30`. It sets display mode `0x42` and copies
128 bytes from each selected record bank into the two original display scratch
buffers. Context zero selects the first bank; every nonzero context selects the
second. The native renderer owns text cursors, font masks and temporary clock
values separately from gameplay state.

| Data | Original address | Format |
| --- | --- | --- |
| Lap records | `0x7a61a` | Two banks of eight 16-byte entries |
| Three-lap race records | `0x7a71a` | Two banks of eight 16-byte entries |
| Selected lap records | `0x1c5ec` | 128-byte display scratch |
| Selected race records | `0x1c66c` | 128-byte display scratch |

Each entry contains twelve name bytes, then BCD minutes, seconds and hundredths.
The sixteenth byte is retained. Record transfer maps the current track through
the original lookup at `0x648c2`; the screen displays the eight resulting rows
in the original order.

The established race timing code updates record names and times when a new
record is set. The competitive return calls `transferNativeLapRecords(false)`
before results, retaining these records for later Hall visits within the same
session. The original competitive caller stores records at `0x5d12a`; its
Practice caller at `0x5cfee` returns directly to menus without that transfer.
The native routes preserve this distinction. Disk persistence is a separate
load/save feature.

## Original-code comparison

`native_hall_records_tests` runs the supplied payload using the test-only
MC68000 oracle and compares native RAM against original outputs:

- All 256 context-byte values for bank selection.
- Both record banks, all eight tracks, load and store directions, and retained
  entry padding.
- 576 race-timing crossings covering twelve player IDs and equal, better and
  slower lap/race records, followed by original record transfer and Hall setup.
- Padded names from one through twelve characters, and exact BCD lap/race times
  for the normal local-player slot.

The final run passed 2,944 comparisons and 1,488,198,272 compared RAM bytes with
zero mismatches. Original timing executes its HUD routines; bitmap and text
cursor outputs are excluded here because the rendering checks cover those
separately. Hall preparation executes through the original bank-copy loop with
the palette fade call skipped and the temporary render-buffer pointer excluded.
Original instruction bytes are unchanged.

The source payload is 408,540 bytes, loaded at `0xe700`, with SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
MC68000 disassembly independently confirms the bank-copy operands and the two
race-return callers. Generated build and verification files remain under
`analysis/hall-records/`.

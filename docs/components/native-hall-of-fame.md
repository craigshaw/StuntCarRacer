# Hall of Fame records and rendering

Record preparation and rendering have separate native/original comparison
projections. The retained scopes below describe those checks; current passes
require matching source, fixture and executable identities.

## Record state

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
The native routes preserve this distinction. The managed player additionally
observes eligible live-race records for
[independent Hall persistence](native-career-persistence.md#shared-records);
legacy HALL transport is disabled there.

## Rendering

`makeNativeHallOfFameImage` draws the original 320×200 record screen from the
local asset pack and session records. It uses the background and palette at
`0x30bf0` and `0x30bd0`, the original primary font, headings at `0x5ec92`, and
track abbreviations at `0x5fde0`. These are runtime addresses in the 408,540-byte
payload loaded at `0xe700`; payload offsets subtract `0xe700`, and source disk
offsets additionally add `0xdc00`.

All eight rows retain their original order, colours, spacing, twelve-character
names and BCD times. Unset records use the original seven hyphens. Context zero
uses the first lap/race record bank; every nonzero context uses the second and
adds its original heading. The entry format and competitive updates are described
above.

Selecting the first main-menu row fades to the Hall palette's entry zero,
prepares the selected records, draws the complete screen, then fades it in.
After release and the original five-unit debounce, a new fire press fades back
to the empty division menu frame before the main options repaint. The native
session reuses the independently verified palette and fire-wait routines.
Their existing host timing limits apply: Amiga rendering cost, bus contention
and incidental idle-poll RNG advances are not reproduced.

## Verification

### Record comparisons

`native_hall_records_tests` runs the supplied payload using the test-only
MC68000 oracle and compares native RAM against original outputs:

- All 256 context-byte values for bank selection.
- Both record banks, all eight tracks, load and store directions, and retained
  entry padding.
- 576 race-timing crossings covering twelve player IDs and equal, better and
  slower lap/race records, followed by original record transfer and Hall setup.
- Padded names from one through twelve characters, and exact BCD lap/race times
  for the normal local-player slot.

The recorded run passed 2,944 comparisons and 1,488,198,272 compared RAM bytes with
zero mismatches. Original timing executes its HUD routines; bitmap and text
cursor outputs are excluded here because the rendering checks cover those
separately. Hall preparation executes through the original bank-copy loop with
the palette fade call skipped and the temporary render-buffer pointer excluded.
Original instruction bytes are unchanged.

### Screen comparisons

`native_hall_of_fame_tests` executes the original `0x5fbc6..0x5fdc8` setup and
rendering instructions without changing code bytes. The harness supplies the
interrupt countdown effects at the original fade spin waits. It compares all
64,000 indexed pixels and sixteen palette words for each of 640 screens:
five context bytes and 128 combinations of names, valid times and unset
records. All 40,960,000 pixels and 10,240 palette words match. It also verifies
that the native renderer leaves gameplay memory unchanged.

The regular game check visits the Hall before and after racing on each track,
checks held fire and debounce, and checks the return to the same main menu.
It also visits after each supported original name-entry setup. Run both pixel
and record comparisons with the full game regression:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_game.py --verify-original
```

Source identities, executable identities, reports and screenshots remain under
ignored `analysis/native-game/`. The source payload SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.

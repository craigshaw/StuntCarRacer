# Native cockpit instruments

The native cockpit uses the original font, masked car graphics, positions and
planar drawing rules. Those bytes come from the locally extracted asset pack.
There are no copied font or icon assets in source control.

`app/native_cockpit_instruments.cpp` ports the original drawing leaves. It
operates on private presentation RAM, including the original two screen
buffers. Gameplay remains in the independently verified native simulation.
Instrument updates happen at their original events; display refresh does not
recompute a completed lap from a clock slot that has already been cleared.

## Original routines

| Original address | Native counterpart | Behaviour |
| --- | --- | --- |
| `0x594c6` | `drawNativeCockpitCharacter` | Primary font, cursor controls, opaque/transparent text, deletion and two-buffer copies |
| `0x5e474` | `drawNativeCockpitLabels` | Original L and B labels |
| `0x5dd86..0x5ddd0` crossing call sequence | `drawNativeCockpitLapCounter` | Lap digit with the original cell and horizontal displacement |
| `0x60846` | `drawNativeCockpitBoostDigits` | Two BCD fuel digits; fuel arithmetic remains in the physics routine beginning at `0x60824` |
| `0x5e326` | `drawNativeCockpitLapTime` | Minutes, seconds, punctuation and optionally visible hundredths, using the original four layout entries |
| `0x5e0c4` | `drawNativeCockpitLeaderIcon` | Original lap/segment/distance tie test and leader graphic |
| `0x5e098` | `drawNativeCockpitResultIcon` | Original finish-result graphic |
| `0x5e12a` | `drawNativeCockpitDistance` | Four decimal distance digits and sign; updates only when the original frame counter is divisible by four |
| `0x5e23e` | `updateNativeCockpitSpeedometer` | Original wrapped word arithmetic, multiply/shift, changed horizontal span, and two-row/two-buffer copy |
| `0x69f42` | internal masked-object drawing | Original mask plus four plane words per object column |

The font advances seven pixels but retains all eight bitmap bits. Opaque text
replaces a seven-pixel cell; the eighth glyph bit may extend into the next cell.
Each glyph copies the two affected 16-bit words from the second buffer into the
first, including surrounding unchanged pixels. This matters when the buffers
hold different data. The menu's transparent text helper cannot replace it.

Original text-mask names are counterintuitive: `setForegroundColor(14)` sets
the opaque cell's paper colour, while `setBackgroundColor(0)` sets glyph ink.
Race entry restores those masks, the primary font and the original labels and
initial opponent icons. Initial boost digits come from the background; a boost
consumption event subsequently draws the current fuel level.

Race entry clears the menu display mode before drawing the cockpit, matching
the overview return at original `0x5d31c`. Primary-font drawing in menu mode
`0x41` subtracts 320 bytes from its planar destination (`0x59624..0x59640`),
which shifts text upward by eight rows. Carrying that mode into the first race
misplaced the static L/B labels, while later digit updates used the correct
race mode. Both first and repeated menu-entered races are now checked against
the original startup drawing, on both screen buffers and all eight tracks.

The speedometer preserves its initial leftmost pixel and modifies the interval
between the previous and current lengths. Its original 128-column wrap is
retained. It is not reconstructed as a newly filled rectangle each refresh.

## Presentation inputs and state

`syncNativeCockpitInstrumentInputs(presentation, gameplay)` copies only these
declared inputs. Its gameplay argument is const:

| RAM | Input |
| --- | --- |
| `0x1bb1c..0x1bb1d`, `0x1bb20..0x1bb21` | Both segments and lap counters |
| `0x1bbb2`, `0x1bbb8`, `0x1bbac`, `0x1bbaf` | Winner bits, opponent-ahead flag, frame counter and completion state |
| `0x1bc38..0x1bc39`, `0x1bc3e..0x1bc3f` | Opponent distance and signed relative progress |
| `0x1bd30..0x1bd31` | Forward speed |
| `0x1ca1a`, `0x1ca20`, `0x1ca32` | Track length, BCD fuel and lap-line segment |
| `0x1c908..0x1c91f`, `0x1c920..0x1c937`, `0x1c938..0x1c94f` | The 24 timer slots |
| `0x57c3c`, `0x5d724` | Original network-mode tie input and display mode |

The following presentation state is deliberately preserved during that copy:

- Both 32,000-byte planar buffers, addressed by `0x6a584`; current drawing
  buffer pointer `0x6a58c`.
- Text masks `0x5982e..0x5983d`; cursor, alignment and parser state
  `0x5983e..0x59844`; font selector `0x5924a`.
- Horizontal/vertical offsets `0x1bb6d..0x1bb6e`, transparency `0x1bbd3` and
  single-buffer flag `0x1bbbc`.
- Current/previous speed lengths `0x1bc18..0x1bc19` and
  `0x1bc20..0x1bc21`; leader/result icon caches `0x1bbde` and `0x1bb8c`.
- Original timer drawing scratch `0x1bb89` and `0x1bbe8`, plus distance-digit
  scratch `0x1bb17..0x1bb1a`.

The timer overload accepts an immutable `PackedLapTime`, visibility, display
position and original time-slot index. It temporarily supplies that snapshot
to the drawing leaf and restores the current slot. Lap crossing and finish
events therefore display their captured times even after gameplay clears or
reuses those slots. Explicit leader/result overloads similarly accept original
car identifier 7 or 11, preserving event ordering.

Normal-race instrument pixels lie within rows 174 through 197. A saved cockpit
strip beginning at row 168 contains every instrument, while retaining the
original background around the individual text cells. Interpolation does not
invent intermediate digits, fuel or lap-counter values.

## Differential verification

Configure the normal oracle build and run:

```powershell
cmake --preset windows-oracle
cmake --build build/windows-oracle --config Release `
  --target native_cockpit_instruments_tests --parallel 4
build/windows-oracle/Release/native_cockpit_instruments_tests.exe `
  analysis/reference/vesuri/offline-extracted-payload.bin
```

The combined check entry point is:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_cockpit.py
```

It builds the instrument, timing and wheel oracles; generates fresh Practice
and competitive races on all eight tracks; checks first and repeated starts
through the menus, the opponent's idle-player finish and natural car contact;
and replays the retained three-lap regression with its captured HUD preserved.
`analysis/native-cockpit/check-results.json`
records source, executable, original-payload, asset-pack and scenario hashes.
The capture is used only by that regression fixture, not native race startup.

The combined suite compares both instrument buffers, initial HUD drawing,
wheel timeline states, sprite placements, restarts, time holds and result
displays. Corresponding gameplay traces are checked independently against the
original. Scenario counts and byte/pixel totals are recorded in the generated
receipt; the original CPU executes only in tests.

The test executes the unmodified original MC68000 routines with Musashi and
compares both complete planar buffers and declared renderer state. The current
result is **12,784 cases, 818,457,248 compared bytes, 13,876,906 original
instructions and zero mismatches**. Coverage includes:

- Every printable primary-font character at all 16 word alignments, opaque and
  transparent cells, both-buffer and single-buffer modes, varied text colours,
  display-mode displacement and backspace.
- All four timer positions, varied BCD bytes and minutes, visible/blank
  hundredths, and immutable finish-time events after slot clearing.
- Every BCD fuel byte, distance signs and the four-frame admission gate,
  leader lap/segment/tie branches, result icons and race-entry initialization.
- 4,096 forward-speed values with varied previous lengths, including the
  original signed clamp and wrap behaviour.
- Source gameplay RAM remaining unchanged during input synchronization, with
  rendered buffers, cursors, icon caches and previous speed length preserved.

This proves the recovered instrument leaves and their pixel writes. The native
presentation integration separately checks when those calls occur. CPU code
execution, custom-chip emulation and self-modifying span instructions are not
part of the shipped native renderer.

## Evidence ledger

Source payload: disk offset `0xdc00`, length 408,540, mapped at `0xe700`;
SHA-256 `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Capstone MC68000 decoding independently cross-checks the instruction ranges.
The enhanced reference source supplied candidate names only; disk bytes and
original execution establish the behaviour.

Retained local REA/Ghidra evidence:

| File under `analysis/rea/` | Evidence ID |
| --- | --- |
| `cockpit-glyph.json` | `ev_cf9ea9bcfaf3afff421197bc485c361799a423e62c28f57ae27e458abcf806cd` |
| `cockpit-speed.json` | `ev_2799a7cb0a111e0e171668c5d4ae70a1ba553283536aa8f9eb1fd91091014e2c` |
| `cockpit-time-drawing.json` | `ev_4ffb8b244eb51312c66b7f14b8902014fb4ca0fd6d10af654abbf72a8273051f` |
| `cockpit-distance.json` | `ev_ea81cbc01db2a1e09dfd12a9a9f4a2e350d71fbbbed5292f4ae8de03d92602d7` |
| `cockpit-boost.json` | `ev_c233e2d42455f1b2e05ed954714b7a9b5bacf9442e20ad86be958fde2a92c599` |

Original media, decoded graphics, executable test builds and reports remain in
ignored local output directories.

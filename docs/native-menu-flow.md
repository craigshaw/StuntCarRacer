# Native original main menu and Practice flow

`app/native_menu_flow.*` expresses the original local menu flow as a small
native state machine. It reads track, raw-key, name and record data from the
validated asset image. There is no emulated CPU or captured state in this code.
The original game-mode chooser and driver-name entry precede this scope: startup
establishes the local main-menu context with the original local-player slot and
its current name. It does not invent a name or implement league/network modes.

## Evidence and addresses

All addresses below are runtime addresses in the supplied disk's plaintext
payload, loaded at `0xe700` from disk offset `0xdc00`, length 408,540. The payload
SHA-256 is `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
For a runtime address `a`, its payload offset is `a-0xe700` and its source disk
offset is `a-0xe700+0xdc00`. The pinned enhanced disassembly supplied names only;
the actual operands and control flow were checked with Capstone MC68000 and
unmodified original instructions in the test-only Musashi harness.

| Original routine or data | Address | Native purpose |
| --- | --- | --- |
| Initial names and record templates | `0x5c890..0x5c8e0` | Copy original name bytes to their backup and initialise both record tables |
| `displayMenu` | `0x5b840` | Menu row defaults and display/input contract |
| Local input body | `0x5b9a4..0x5ba3e` | Confirm priority, numbered keys, directional selection |
| Busy-loop delay | `0x5ba42..0x5ba68` | Original release/debounce timing order |
| Practice caller | `0x5ba78..0x5baae` | Selected division/track, Cancel and per-track configuration |
| Main menu | `0x5baea` | Default Practice selection and repeat-Practice shortcut |
| Division/track selection | `0x5fb62..0x5fbc6` | Reversed division display order and track menu |
| Set current track | `0x5bcf0..0x5bd1e` | Read original track ID and configuration tables |
| Menu system loop | `0x5cfb8..0x5d000` | Preview, race and repeat-Practice route |
| Preview input | `0x5d980..0x5d9da` | Release mask, rotate and start |
| Wait for fire | `0x6488a..0x648b2` | Fire release, five-unit debounce, then fire press |
| Original numeric raw-key table | `0x60c8d` | Keys 1 through 4; differs from provisional upstream equate |
| Track lookup/configuration tables | `0x648c2`, `0x648ca` | Actual division-to-track mapping and car-context configuration |

REA's parent investigation independently retained main-menu evidence
`ev_9539d4ee01b56359869005349407aed5b150f2283f6a4e60ba71cc1b0d81b0ba`.
Local Capstone listings and the flow test report are under the ignored
`analysis/native-menu-flow/` directory.

## Original behaviour retained

The main menu has four rows: the original records, Practice, racing-season and
load/save/replay choices. The initial cursor selects Practice. Numbers select a
row, then Return/fire confirms. Directional input clamps at either end, with up
taking priority when up and down are held together. Simultaneous number keys are
resolved in descending row order. Changing a selection repaints the cursor and
waits for release; holding a direction does not repeatedly scroll.

The original's `D2` parameter is one less than the number of painted/selectable
rows. Main and division menus use `D2=3` for four rows. The track menu uses
`D2=2` for two tracks and Cancel. Its `maxMenuIndex` byte is another one smaller;
the input loop intentionally admits `maxMenuIndex+1`.

Practice presents four divisions, with default row `currentDivision XOR 3`.
Confirming a row stores `selectedDivision=row XOR 3`. The two track IDs come
from the original pack's lookup table. Successful selection stores the track
index, sets `divisionSelectedFlag=1`, and reads the associated configuration
byte, with the original alternate car-context flip.

The separate `NativeFireWait` helper implements the original post-race and
screen-continue wait. Only fire must be released; holding gas or a direction
does not block it. After a five-unit debounce it accepts fire, including a press
that began during the delay and is still held when it ends.

The preview waits for the original button-release mask to clear. Left increments
the camera angle byte, right decrements it, and fire starts the race. Left takes
priority over right and fire. Angles wrap as bytes. Escape is not an original
preview Cancel binding. The host may use the explicit `cancelPreview()` recovery
method when an unavailable native feature has to return control to the menu;
that method is not represented as an original menu route.

After Practice the menu system returns directly to the same division's track
selection, because the previous selection set `divisionSelectedFlag`. The
division screen is skipped once and the flag is cleared. Selecting Cancel then
returns to the main menu. The race-exit cleanup and persistent session memory
belong to the host session, which passes the same memory back to this flow.

Unported main-menu routes yield `UnsupportedOption` while retaining the original
menu selection. The native host reports that boundary; this implementation does
not silently substitute a different mode. All eight original track mappings are
retained; availability of the moving drawbridge simulation is a host boundary.

## Timing and verification limits

The original delay costs `154280*units+18` 68000 clocks when its initial byte
counter is zero, including its RTS and excluding caller overhead. A nonzero
initial counter shortens the first inner pass by `(256-counter)*30` clocks.
The native flow retains this too: track-name rendering leaves that counter at
15, shortening its first repaint delay by about one millisecond. The native
flow uses PAL CPU frequency 7,093,790 Hz.
The 15-unit repaint wait is approximately 326 ms, then the original code waits
for all controller input to be released and delays another two units, about
43.5 ms. Track entry adds the original 20-unit delay. Rendering time and Amiga
bus contention are not included in these CPU-only timing measurements. The
native host samples input during its display loop; it does not reproduce the
original free-running polling loop's incidental RNG advances while idle.

`tests/native_menu_flow_tests.cpp` links Musashi exclusively in its verification
executable. It checks:

- 3,584 input combinations against original instructions, comparing all 512 KiB
  after each pass: 1,879,048,192 byte comparisons, zero mismatches.
- Original 1/2/15-unit delays of 154,298 / 308,578 / 2,314,218 clocks, with the
  verification CPU's synthetic 40-clock reset charge explicitly excluded.
- 48 original Practice-caller cases covering both car contexts, every division,
  each track and Cancel, and the repeat-Practice division shortcut. The harness
  supplies selected-row return values at the separately verified menu callee;
  the caller's instructions and track lookup remain unmodified.
- 96 original preview-control cases covering every controller mask and byte-angle
  wrapping. The harness supplies normalized controller results at the original
  input callee; the original preview routine decides rotation/start/release.
- 64 original fire-wait cases covering every normalized mask, initially held or
  released fire, and the five-unit delay. Native checks also verify that a press
  begun during debounce is accepted when it ends and that reset rearms the wait.
- Original default-name and record-template copies, plus a native full route
  through main, divisions, tracks, preview, race, repeat selection and Cancel.

These are control-flow and memory comparisons, not a claim to have booted the
complete original menu subsystem natively without the listed scope boundaries.
Pixel rendering is compared by the separate native-menu graphics oracle.

# Opening flow, name entry and screens

`NativeOpeningFlow`, `NativePlayerNameEntry` and `NativeOpeningNameScreen` own
the mode/name/roster journey. They consume the local asset pack and native
state; no original CPU or captured screen is used by the player. The direct
Practice entry bypasses this journey and remains a separate development tool.

Single Player first opens [career selection](native-career-persistence.md).
Only New Career enters name/roster setup; Continue and Browse open the selected
career's main menu. The recovered sequence below also defines the baseline
and multiplayer paths used by original-code checks.

## Flow

Startup initializes name and record templates, restores the 256-byte name
buffer, selects local context/game state `0x40`, and performs the game-data
reset. The menu frame suppresses division text before drawing the mode chooser.

1. Single Player accepts one name and shows the twelve-driver portrait roster.
2. Multiplayer accepts the first name, then offers Add Player / Continue.
   Continue requires two names; the eighth completed name ends entry
   automatically. Local standings follow, retaining all names and player count.
3. Computer Link enters a serial protocol in the original. The native chooser
   displays its row in grey and disables navigation, its numeric shortcut and
   confirmation. No transport dialog is entered.

After the released/new-fire wait, startup saves RNG state and sets protocol
state `0xf3`. The local handshake returns without changing game memory; the
main menu then opens. `NativeOpeningFlow::nameCompleted` receives completion
from the separate editor. The host owns palette transitions and pauses the
Original flow while they run.

The first driver is ID 11; additional names descend through IDs 10–4. Local
Practice keeps ID 11 and context zero without replacing the name or player
count. Local standings cover IDs `11-additionalPlayerCount` through 11 and break
equal scores by descending driver ID, without consuming RNG. Single-player
season tie-breaking is a different contract.

## Name input

The prompt routine at `0x5b648` calls the editor at `0x5b6de`. An initial release
gate discards held input. Entry starts at position zero, with twelve characters
and record offset `(11-additionalPlayerCount)*16` in the array at `0x1ecab`.
The editor position stays separate from RAM, matching the original register.
Declared RAM scratch fields retain their recovered meanings.

- Letters retain case; Shift selects the uppercase table through `0x1b894`,
  otherwise the table at `0x1b890` is used.
- Digits, spaces and `.`, `/`, `:` are accepted. Leading spaces are ignored;
  trailing spaces remain. Extra printable input at twelve characters is consumed
  and ignored, while Backspace still works.
- Backspace at zero does nothing; elsewhere it moves back and emits erase
  character `0x7f`. The stored byte changes only on later input or final padding.
- Return, keypad Enter and controller fire finish and space-pad the name.
  An empty submission retries after release; no default name is invented.
- The recovered editor does not cancel ordinary name entry on Escape or
  directions. Responsive setup Back is a host route around this editor.
  The generic editor's signed directional mode at `0x62bb5` retains distinct
  Escape/up/down/left/right results and uppercase conversion at record `0xc0`.

The scanner at `0x5985c` searches raw keys from 127 down to zero and consumes
only pending value `0xb3`, clearing the highest eligible key first. Shift codes
`0x60`/`0x61` remain held and are excluded from that search; any nonzero Shift
byte selects uppercase. Caps Lock does not toggle case. With no ordinary key,
controller priority is up, down, left, right, fire. Normalized host bits are
translated to the active-low byte at `0x1b88e`; fire yields encoded pair `0x440d`.

`waitForInputPress` at `0x59850` waits for availability, with no software repeat
clock or character-animation delay. Host key-down/up events use Amiga raw codes
and exclude host autorepeat. Each ordinary key-down is consumed once; held
controller input remains available. Text polling is independent of racing's
120 ms updates. The enhanced reference's default `Player` name and extra
key-release loop are absent from this disk.

`begin()`/`update(controls)` return drawing events: `beganAttempt` places the
prompt/cursor, `characters` emits glyph/erase events, `complete` restores exit
colour/offsets, and `retriedBlank` marks another attempt. Input does not mutate
the renderer's cursor, font or buffers. Padding stored names draws no spaces.

## Rendering and timing

The name screen uses menu pixels at `0x23c52`, palette `0x23c32` and initialized
font `0x1fe82`; asset decoding/font patches belong to `initializeNativeAssets`.
`drawScreenFrame` is `0x64c6e`, `NAME?` is at `0x64354+0xe0`, and cursor object 2
on menu item 1 supplies the empty panel. `drawHorizontalLine` draws colour 10
at y=133, x=106 through 189. No host font is substituted.

The arrow begins at column 14, row 16, with a four-pixel text offset. Glyphs
retain transparent mode. Erase temporarily clears an opaque seven-pixel cell
with colour 2, then restores transparency. All eight font-row bits survive
the seven-pixel character advance. Blank submission keeps the screen, resets
the position and redraws the arrow. Repainting cannot repeat input or advance it.

Mode and Add Player use `makeNativeMenuImage`, offsets `0x10` and `0x14`, and
the original selected/confirmed strips. The game logo/scenery remain part of
that menu image. Computer Link uses a native `disabledRows` mask: grey text,
unselected strip, and rejected input. Other menus/oracle checks use an empty mask.

Original paint/confirmation waits fifteen busy-delay units, then releases all
menu controls and waits two units before another input. `nativeMenuDelaySeconds`
uses the measured PAL loop; palette fades advance at 50 Hz. Responsive
navigation removes these waits through the same transition handlers; see
[menu navigation](native-menu-flow.md#responsive-navigation).

The portrait roster fades to its first colour `0x0222`, loads/draws portraits,
then fades to their palette. After fire, `displayMenuScreen` fades to the menu
palette's first colour (forced black at startup), restores the frame and fades
in before painting main options. Local multiplayer standings instead replace
the frame directly and wait for fire with the menu palette, without either fade.
They use `makeNativeOpeningStandingsImage` after native sorting and write 16 to
row byte `0x64b02`. Panels copy overlapping original strip rows; name/statistic
spacing remains unchanged. Later multiplayer race-result tables are separate.

## Recovered evidence

Runtime addresses refer to the 408,540-byte payload at disk offset `0xdc00`,
loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Payload offsets subtract `0xe700`; disk offsets additionally add `0xdc00`.

| Address | Recovered operation |
| --- | --- |
| `0x5c890..0x5c8e0` | Name backup and record templates |
| `0x5cf46..0x5cfb8` | Startup, mode/name/roster order and saved context |
| `0x5e8ac..0x5e93e` | Mode selection and local-player loop |
| `0x648b2` | Portrait roster or local standings selection |
| `0x58888..0x58914` | Portrait roster fade/wait calls |
| `0x646e8` | Local standings and released/new-fire wait |
| `0x5ab00`, `0x5acfe` | Local table bounds and score sorting |
| `0x6368c` | Saved RNG, including overlapping byte destination |
| `0x579b0`, `0x57ab4` | Local no-network handshake |
| `0x5b648..0x5b83c`, `0x64b0e..0x64c18`, `0x6642a..0x6646c` | Name entry, text and drawing ranges cross-checked with MC68000 Capstone |

Retained REA dossiers identify:

- Mode: `ev_d39905317e82c62620f255b3860480fcddc2336e84ab1b50a8689e2a29b176cb`.
- Initial startup: `ev_207fae22b1202bd46e7484a29e8700b6c1529f18d1e97525f4cf1af2f8e464db`
  (incomplete body); outer caller:
  `ev_955de4dbd780773e42159fc6ac57aa7caa70ed7bace9d0413156ec1f1592faba`.
- Menu fade order: `ev_9bc7a62d8715bd540eb3f0e6b298e8334052bdcfe895ab8fa3fc33a22dbb05b9`.
- Name entry: `analysis/rea/opening-2.json`,
  `ev_01868ff2a11dcd206bbfae2022d06f665e6c0deab2b1cc81bd437570651e1f6a`.

Capstone and unchanged original execution corroborate these boundaries.
Enhanced upstream labels alone do not prove disk instructions or blank-name behaviour.

## Verification

The following are recorded suite scopes, not claims about an unidentified build:

| Suite | Projection and recorded coverage | Local evidence |
| --- | --- | --- |
| `native_opening_flow_tests` | Complete 512 KiB RAM for templates, sorting and saved context; 896 standings cases and 32 scripted mode/name routes; 487,063,552 bytes and 387,205 original instructions, zero mismatches | `analysis/opening-flow/report.json` |
| `native_player_name_tests` | 24,576 scanner cases, 12,300 editor cases and 48 complete input sequences; 6,343,692 bytes and 7,744,719 instructions, zero mismatches | `analysis/player-name/report.json` |
| `native_opening_render_tests` | 985 cases: 40 mode/add-player, 833 panel/glyph/erase/retry, 112 local standings; 63,296,000 pixels and 16 palette words per case; 31,788,462 instructions, zero mismatches | `analysis/opening-render/` |

Flow routes script separately verified menu/name callees and check rejected
one-player continuation and automatic eighth-player exit. Scanner comparisons
enter at `0x59862` after the hardware read; editor comparisons enter at `0x5b752`
after the wait. Full sequences execute unchanged `0x5b6de` with keyboard snapshots
at scanner calls. Character rendering runs normally; glyph calls are observed,
not stubbed. Cases cover every raw key/controller mask, Shift/table indirection,
simultaneous keys, all character bytes at empty/near-full/full positions, every
player slot, blank retries, held fire, directional mode and mixed-case editing.
Pixel checks compare both buffers before input and the active buffer while editing.
Hardware interrupt timing and Windows presentation are separate boundaries.

Run `scripts/check_native_opening.py` through the Python environment described
in [development](../development.md#choose-verification-by-change). Its combined
receipt in `analysis/native-opening/check-results.json` pins sources, executables,
pack and payload, and includes 36 names, all seven local-player counts and three
named race routes (195 gameplay updates compared with the original full loop).
Generated assets, captures, reports and images stay under ignored `analysis/`.

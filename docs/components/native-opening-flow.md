# Original opening flow

The native session now retains the original game-mode chooser, driver entry,
add-player loop and opening roster before the main menu. The existing direct
Practice startup remains useful for replay and physics checks; it does not
pretend that the opening menus have already run.

The recovered sequence below describes the baseline. In the current player,
Single Player first opens [career selection](native-career-persistence.md).
Only New Career enters this name/roster journey; Continue and Browse Careers
open the selected driver's three-row main menu. Multiplayer setup is unchanged.

The native flow takes normalized original controller bits and elapsed time.
The host freezes it while the original palette transitions run, owns the
separate name-entry component, and reports a completed name through
`NativeOpeningFlow::nameCompleted`. No original CPU executes in the game.

## Recovered order

The original startup initializes names and record templates, restores its
256-byte name buffer, selects local context and game state `0x40`, and performs
the original game-data reset. It displays the menu frame with its normal
division text suppressed, then displays the mode chooser:

1. Single Player enters one name, then shows the twelve-driver portrait roster.
2. Multiplayer enters the first name, then offers Add Player / Continue. A
   one-player multiplayer session cannot continue: it returns to that menu.
   The eighth completed name automatically ends entry. Local standings follow.
3. Computer Link enters the serial-link protocol in the original. The native
   chooser keeps its row visible in grey but disables selection and confirmation,
   including its numbered shortcut. No unavailable-transport dialog is shown.

After the roster's released/new-fire wait, startup saves the original random
state and sets network protocol state `0xf3`. The local handshake path returns
without changing game memory. The main menu then opens. Local multiplayer
retains all names and its player count; racing a multiplayer season is a
separate feature from this opening sequence.

The first driver is player ID 11. Additional names descend through IDs 10 to 4.
The original local Practice configuration keeps player ID 11 and context zero;
the startup flow does not replace the name or force the additional-player count
back to zero. Local standings use table range `11-additionalPlayerCount` to 12
and break otherwise equal scores by descending driver ID. Unlike single-player
season ties, these local ties do not consume random numbers.

## Appearance and timing boundaries

The chooser and Add Player menu use the existing original-asset menu renderer,
with string offsets `0x10` and `0x14`. Each paint and confirmation runs the
original fifteen-unit busy delay. The caller then waits for all menu controls
to be released, runs the original two-unit delay, and admits another input.
These use `nativeMenuDelaySeconds`, which is measured from the original PAL
68000 busy-loop code. Palette fades use the independently recovered 50 Hz
palette routine.

In both navigation policies the chooser marks Computer Link in the native-only
`disabledRows` mask. The renderer uses the unselected strip and grey text; menu
input skips disabled rows and rejects their numeric keys and confirmation.
Other menus and original-code rendering checks use the default empty mask.

The single-player roster fades to the portrait palette's first colour,
`0x0222`, loads and draws the portraits, then fades to their palette. After
fire, `displayMenuScreen` fades to the menu palette's first colour, forced to
black at startup, restores the frame and fades in. The main menu is then drawn.
Local multiplayer standings replace the menu frame directly and wait for
fire, retaining the menu palette; the original does not add a fade at either
side of that table.

## Original-code evidence

Source payload: disk offset `0xdc00`, 408,540 bytes, loaded at `0xe700`.
SHA-256: `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.

| Original address | Recovered operation |
| --- | --- |
| `0x5c890..0x5c8e0` | Name backup and both record templates |
| `0x5cf46..0x5cfb8` | Startup entry, mode/name/roster order, saved context |
| `0x5e8ac..0x5e93e` | Mode selection and 2..8 local-player loop |
| `0x648b2` | Select single-player portrait roster or local standings |
| `0x58888..0x58914` | Portrait roster and its exact fade/wait calls |
| `0x646e8` | Local standings setup and released/new-fire wait |
| `0x5ab00`, `0x5acfe` | Local table bounds and score sorting |
| `0x6368c` | Saved random state, including overlapping byte destination |
| `0x579b0`, `0x57ab4` | Local no-network handshake branch |

REA dossier IDs for the mode and initial startup routines are
`ev_d39905317e82c62620f255b3860480fcddc2336e84ab1b50a8689e2a29b176cb`
and `ev_207fae22b1202bd46e7484a29e8700b6c1529f18d1e97525f4cf1af2f8e464db`.
The latter has incomplete body coverage. The outer-loop dossier
`ev_955de4dbd780773e42159fc6ac57aa7caa70ed7bace9d0413156ec1f1592faba`
and independent Capstone MC68000 decoding supply the missing caller. The
menu-screen fade order is retained in dossier
`ev_9bc7a62d8715bd540eb3f0e6b298e8334052bdcfe895ab8fa3fc33a22dbb05b9`.
Enhanced upstream source labels are navigation aids, not proof of
instructions in this disk version.

`native_opening_flow_tests` executes the original template initializer, local
setup/sort and saved-context suffix under the verification-only Musashi CPU,
comparing the entire 512 KiB game-memory image. Mode-route checks execute the
original mode caller with independently tested menu and name routines replaced
by scripted results, checking all one-to-eight-player call sequences, rejected
one-player multiplayer continuations, and the maximum-player automatic exit.
Delay-boundary checks exercise the native caller's paint, release and confirm
phases using the original delay durations. Pixel rendering, text filtering and
keyboard timing have separate original-code checks.

The flow check passes 896 local standings cases and 32 complete mode/name-call
routes, with 487,063,552 RAM bytes compared and 387,205 original instructions
executed. There are zero mismatches. Its generated receipt is
`analysis/opening-flow/report.json`.

Generated reports and all source game assets remain below ignored `analysis/`.

The combined `scripts/check_native_opening.py` check records source, executable,
asset-pack and payload hashes in `analysis/native-opening/check-results.json`.
It also exercises 36 names through the complete native opening, all seven local
player counts, and three named Practice/season race routes. Those 195 gameplay
updates match the original full loop. Static input cases, original screen pixels
and complete native sessions are distinct checks in that receipt.

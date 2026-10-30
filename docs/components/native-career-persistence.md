# Native careers and shared Hall persistence

The Windows player enables `NativeGameSession::enableCareers` at the mode
chooser. Single Player always opens career selection. New Career is always
present; Continue requires an available last-used career, and Browse Careers
requires saved careers. Continue and Browse open the selected driver's three-row
main menu: Hall, Practise and Start Season/Resume Season. Back returns to selection;
the fourth Career row is removed. The list is ordered by last played, scrolls
without a thirty-slot limit and permits duplicate driver names. Division, league
and date distinguish entries. Delete confirms removal of the selected head and both recovery
copies. Removing the last-used career leaves Continue unavailable until another
career is explicitly chosen. Names retain the original editor's twelve-character
limit and case rules. No rename, original manual-slot import, single-player manual
save/load/Replay or multiplayer autosave is provided. Managed career autosave
applies independently of navigation timing and driving preset.

Creation remains a setup draft until its first successful save; cancelling the
name/roster journey registers no career. Successfully creating or opening a
career updates the last-used identity. Browsing the list or writing Hall records
does not select a different career.

`app/session/native_career_session.cpp` owns the host continuation/UI policy.
`native_career_checkpoint.*` encodes explicit menu state and changed native-byte
ranges against deterministic opening assets. The header pins the baseline hash;
version, bounds, driver order, league, damage, round and supported continuation
pages are validated before changing the session. The neutral `NativeMenuFlow`
restore constructor does not run any game handler. An open main menu can retain
one separate suspended-season checkpoint; Practice and menu state cannot replace
that season. Input latches are released on
restore. Display, navigation and driving settings belong to the host and remain
shared. Original arithmetic, race logic and clocks are unchanged.

## Checkpoint boundaries

- Creation requires a durable first checkpoint after the opening roster.
  A failed first save offers Retry/Cancel before racing is available.
- Menu progression saves fresh career, matchup, prepared preview, finish,
  results, records, round results, standings, season end, award and roster state.
  Back suspends the current stage to the selected career's main menu in either timing policy;
  preview Back returns to its prepared matchup. It never acknowledges a report.
- While driving, the prior checkpoint represents the same pre-race matchup.
  Closing or crashing restarts that fixture through its preview, with its original
  pre-race damage/RNG. Game menu Leave Race selects the quit outcome immediately
  without another physics tick, including during crash/recovery. Its resulting
  damage, RNG and scoring still pass through the normal exit and checkpoint path.
- At a competitive terminal message, `RaceScene::copyLiveExit` stages exit on
  a private copy, and a separate menu instance scores that copy. The checkpoint
  retains the outstanding finish frame and resulting report. The running game
  still applies its normal exit once; a restored finish acknowledgement reveals
  the already-scored report. Full RGB finish pixels include hardware sprites.
- Continue and Browse always open the main menu. Resume Season restores its
  independent checkpoint, including an unacknowledged finish. It never reruns
  season setup. A season stops being resumable after its final roster/reset.
- Practice retains its resulting main-menu RNG and career fields, returns
  normally to track selection, and never checkpoints an active Practice race or
  adds Hall records. A suspended season retains its own RNG, fixture and reports
  unchanged even when Practice is interrupted or the application is restarted.

Practice preserves division, driver order, league and pre-race damage. It cannot
advance a pending fixture or repeat scoring/promotion/reset. A paused terminal
finish remains distinct from an unfinished race, including during application exit.

| Career Back route, in either navigation policy | Destination |
| --- | --- |
| Competitive preview | Prepared matchup |
| Matchup or outstanding finish/report/award | Career main menu, with the exact season suspended |
| Career main menu | Career selection |
| Career selection | Mode chooser |

Back never forfeits a fixture or acknowledges a report. Resume Season restores
that stage; A/Enter advances it. The host Game menu has no Return to Careers
action; Leave Race retains its separate quit/scoring consequences.

## Shared records

Both modes use one authoritative pair of normal/Super Hall banks. The observer
copies the original timing routine's human lap/race candidates after competitive
steps; it performs no timing, physics or RNG updates. Strictly better valid BCD
times replace holders, while ties retain them. A completed competitive human lap
can therefore survive closure during the unfinished race. Practice, opponent-only
and partial-lap timings do not become candidates. Old career checkpoint banks
are replaced with the authoritative Hall on load. Career deletion cannot remove
records. Legacy HALL operations are disabled in the managed player, while
multiplayer progress and access saves retain their manual codec.

## Storage and failures

Normal player data lives under `%LOCALAPPDATA%/Stunt Car Racer/`:

| File | Content |
| --- | --- |
| `careers/<random identity>.scr-career` | Driver metadata, last played, continuation |
| `last.scr-index` | Explicit last-used identity; no fallback to another driver |
| `hall.scr-hall` | Shared lap and race records in both leagues |

Each file has a bounded, checksummed, versioned envelope. A serial worker writes
immutable jobs to a sibling `.tmp`, flushes it, then replaces the head with
`MoveFileExW` and write-through. `.bak1` and `.bak2` retain previous valid
generations; a missing/corrupt head selects the newest valid backup and notifies
the player. Unknown formats remain untouched. Files whose native checkpoint body
fails validation are excluded from recovery and rotation too. Stale `.tmp` files
are never loaded. Deletion drains pending jobs before removing every generation.

Native checkpoint version 2 adds a length-bounded suspended-season payload to a
main-menu checkpoint. Nested suspensions are rejected, and both states validate
before application. Existing version-1 native careers remain readable and open
at the main menu with Resume Season where appropriate. This does not import
original Amiga/manual-slot saves. Envelope and Hall formats are unchanged.

Failures retain RAM and the last good disk checkpoint. Not saved remains visible,
with Retry/Continue without saving at a safe menu; driving prompts are deferred.
Continue for the active career uses its latest in-memory checkpoint. Future
boundaries retry failed writes. Changing careers or closing drains jobs and offers
Retry/Leave unsaved/Cancel on failure. A damaged Hall needs explicit fresh-start
consent; the old files are copied to `.damaged-<timestamp>` before replacement.
An unsupported Hall cannot be reset by this version.

Saving/Saved feedback belongs to menus, pause screens and the window title;
successful Hall writes add no driving popup. Saved is shown only after worker
completion. Continue without saving retains the warning and latest in-memory
season for Back/Continue/Resume, retries at later boundaries and suppresses
repeated prompts. Career and record failures remain distinguishable. If all
career generations are unusable, preserve them and make that career unavailable
until explicit recovery/deletion rather than silently resetting it.

`--data-dir` isolates career/Hall storage and instance ownership. Development's
`start-native-game.ps1 -DataDirectory` defaults to `working/player-data/`.
The asset pack, legacy manual saves and settings have their own existing overrides.
Only one player process may use a data directory; a second launch brings its
window forward. The storage worker never advances simulation or render history.

## Verification

`native_autosave_tests` covers native-byte/RNG roundtrips for nine continuation
pages in all eight division/league contexts, fresh-launch and in-memory Continue,
interrupted races, a real opponent finish before acknowledgement, pending report
images, duplicate names, deletion, recovery copies, malformed/unsupported files,
atomic-write failure and retry, first-save failure, unsaved RAM continuation,
and synthetic eligible human lap/three-lap handoffs in both leagues and both modes.
It also checks 36-career scrolling, setup draft cancellation, Practice in both
navigation/drive policies, and fresh-launch season endings/final resets in every
division and league. A restored finish is compared with uninterrupted scoring.
The revised menu checks include conditional entry rows, three-row navigation,
Continue/Browse landing at the main menu, Resume Season after Practice and
relaunch, preservation of a pending finish during interrupted Practice, and
version-1 native continuation plus bounded nested-state validation.
Synthetic fixtures are explicitly separate from the uninterrupted real race.
The existing native career/multiplayer and original-code suites retain their
rule and race comparisons. `check_native_game.py` includes the autosave suite in its
source/executable receipt and stages the verified player through CMake Install.

The [architecture decision](../adr/0001-native-career-checkpoints.md) records why
native checkpoints and independent Hall storage replace legacy transport.
[Original career rules](native-career-state.md) and the
[manual codec](native-save-state.md) retain separate recovered contracts.

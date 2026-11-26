# Portable player data and multiplayer saves

The accepted design uses convention-based loading with no saved-data import,
layout conversion or file-management UI. This applies to the launcher and game.
Stages 1–5 and the simplification are implemented; stage 6 final acceptance remains
pending. [ADR 0004](../adr/0004-portable-player-data.md) records ownership;
[sessions](sessions.md) and [multiplayer checkpoints](multiplayer-checkpoints.md)
record the runtime contracts.

## Ownership and folder convention

One `data/` directory beside the executable holds assets, settings, saved games
and records. Everyone using that game folder sees the same data, regardless of
Windows account. Moving the folder carries the complete game; separate copies
are independent. There is no portable marker, Windows profile discovery, implicit
AppData fallback, migration receipt or automatic synchronisation.

```text
Stunt Car Racer/
  Stunt Car Racer.exe
  LICENSE
  THIRD_PARTY_NOTICES.md
  data/
    assets/
      stunt-car-racer.scr-assets
      ... pack provenance
    settings.ini
    careers/
      Craig-<short-id>/
        career.scr-career
        personal-bests/
          original/
          enhanced/
    multiplayer/
      Friday Night-<short-id>/
        game.scr-multiplayer
    hall-of-fame/
      original/
        index.scr-hall
        ghosts/
      enhanced/
        index.scr-ehall
        ghosts/
```

Each file's recovery generations live beside it. Existing Original/Enhanced
checkpoint, PB and Hall formats keep their own validation and version rules.
There is no conversion of old flat career/PB layouts, Windows profile storage,
ProgramData archives or original multiplayer slots.

Discover immediate child folders by `career.scr-career` or
`game.scr-multiplayer`, including their `.bak1`/`.bak2` recovery files. Validate
before loading. Ignore unrelated files, empty/unfinished folders, nested histories
and links. Preserve damaged or unsupported saves and show them as unavailable
through the existing chooser; never overwrite them as though they were empty.
No manifest or central catalogue is needed.

Initial folder names come from the driver/game name plus a cosmetic suffix.
Renaming a folder does not rename its driver/game or alter native state. The full
internal ID binds its checkpoint and private recordings, rather than choosing a
folder to write. Two folders with the same internal ID are independent saved
games: discover both, load the selected folder and write only there. Never merge,
rekey, replace or resolve copies automatically, and offer no conflict actions.
Original/Enhanced PB caches are cleared when switching career folders so matching
IDs cannot mix records in memory.

Separate last-used folder names belong in coordinated settings preferences.
Losing settings does not hide saved games. Renaming/removing the last-used folder
may remove Continue until the player chooses a game; it must not select another
copy by date or matching ID. Folder paths never enter native checkpoint bodies.

Copy, rename, replace and delete folders with the game closed using the operating
system. The existing Browse choices select games directly. No extra action menu,
folder path display, import prompt, fresh-start choice or save-management controls
belong in the launcher/game. Keep ordinary gameplay New, Continue and Resume.

| To carry | Copy |
| --- | --- |
| Ready-to-play installation | Complete game folder, including `data/` |
| All player data | `data/` |
| One career with PBs, ghosts and recoveries | Its complete folder under `careers/` |
| One multiplayer game | Its complete folder under `multiplayer/` |
| Shared records and rivals | Complete `hall-of-fame/` with both categories |

Copied games require compatible assets. Loading them never submits historical
records as new Hall attempts. The Hall remains independent of saved-game progress;
there is no archive merge feature. Normal play does not enable development race
recording.

## Autosave and continuation

### Careers

Retain existing checkpoint semantics: Continue opens the career home, Resume
Season restores its independent suspended season, and Practice/Hall challenges
cannot advance that season. Keep supported older checkpoint versions readable,
including pending Original/Enhanced finish displays and record notices.

Copy the career and both PB categories together without converting their game
state, recalculating records or invoking original save/progression handlers.
The folder change alone must leave simulation state and RNG untouched.

### Multiplayer games

Create a named-game draft while collecting the roster. It becomes a saved game
only after two to eight drivers are confirmed and the first durable checkpoint
succeeds. Retry or cancellation handles failure before racing, as for a new
career. The game title is host metadata separate from the original driver names.

New Game also selects Division 4, 3, 2 or 1 and Normal or Super League. Every
combination is available without a career or access-file import. The selected
division supplies its original two-track championship; this does not introduce
arbitrary track combinations. Store the configuration with the draft and native
checkpoint, and retain it through subsequent championships and Resume. A copied
multiplayer game is self-contained; deleting, restoring or advancing a career
cannot change its configuration or availability.

Continue opens that game's home. Resume returns to the saved competition stage;
starting another championship follows existing rules and retains cumulative
state. Returning home or browsing another saved game must not acknowledge reports
or run the next turn as a side effect. The same actions are available under both navigation modes.

The native multiplayer checkpoint must retain:

- The complete roster, turn order/current human, per-driver damage, selected
  division/league and dependent track configuration.
- Scores, race/track results, bonus-award state, current track/turn and the
  four-championship accumulation/reset counter.
- Both gameplay RNG stores, prepared-matchup/preview state, the exact pending
  menu/report stage and host continuation flags such as `multiplayerTrackEnd`.
- Pending finish presentation and available Original/Enhanced timing/record
  notices where the current multiplayer journey presents them.

Capture after confirmed setup, each prepared pre-race matchup, terminal race exit,
committed report/turn/track/championship transitions and safe departure. An
unfinished race keeps its already captured pre-race checkpoint; earlier humans'
completed turns remain committed. Resuming never restores a car mid-race.

Stage terminal exit/scoring on a private copy before acknowledgement, using the
same principle as career capture. Restoring that stage must neither rescore the
race nor reapply damage, bonuses, turn advancement, the championship reset or
live Hall delivery. Resume/preview must not prepare the same race twice.

Replace the normal thirty-slot virtual disk, special `MP` names and format prompt
with these native saved-game routes. The player has no legacy multiplayer
load/save or access-slot route. Original codec/progression routines may remain
for their verification tests; they do not require a player-facing compatibility
feature. The original race rules, multiplayer turn order, pace car, scoring and
reset behaviour stay authoritative. Open division/league selection is a deliberate
change to multiplayer content access; single-player progression is unchanged.
Computer Link remains deferred.

## Storage and host boundaries

Resolve the root from the executable directory independently of its basename,
working directory or shortcut. `--data-dir` overrides the entire layout once,
including both Hall categories. `--recordings-dir` explicitly enables developer
recording. Individual player save-path and import overrides are unsupported.

Acquire process-held root ownership before launcher writes. Aliases identify the
same directory, crash releases the lock, and a second launch activates the existing
window where possible. Roots inherit ordinary Windows permissions. Unwritable
storage reports failure through existing startup/save handling without falling
back to another root or changing ACLs. A real second-account check remains pending.

Careers and multiplayer share a serial writer with immutable, path-addressed jobs.
Use flushed temporary replacement, checksums, bounded files and two valid recovery
generations. The first atomically written checkpoint makes a new folder discoverable;
no multi-folder transaction or journal is needed. Cancelling a new-game draft drains
its jobs and removes only that reservation's own files, preserving unrelated data.
Discovery and recovery never modify unsupported files.

Saved means durable worker completion. Switching games/closing drains writes;
failure retains the latest RAM candidate and the last good disk copy. Existing
Retry, Continue without saving and departure choices remain. Capture, validation,
discovery and presentation do not consume gameplay RNG. Timing remains 120 ms
updates, 50 Hz wheel/engine state and 60 Hz host presentation.

## Implementation and remaining acceptance

1. **Layout and ownership — implemented.** Whole-root override, canonical aliases,
   explicit Hall roots, inherited access and process-held crash-released ownership.
2. **Self-contained careers — implemented and simplified.** Convention discovery,
   folder-local PBs/backups, independent copies and coordinated last-used preferences.
3. **Native multiplayer checkpoints — implemented.** Validated native memory/menu
   continuation, exact-once terminal scoring and pre-race interruption checkpoints.
4. **Named multiplayer games — implemented and simplified.** Draft setup, all
   division/league combinations, autosave, Continue/Resume and direct Browse selection.
5. **Portable defaults — implemented and simplified.** Executable-local data for all
   player launches, complete development override and no conversion/import UI/backend.
6. **Final acceptance — pending.** Validate the simplified release across physical
   keyboard/controller input, both navigation modes, a real second Windows account,
   copied/moved installations, another drive, renamed EXE and shortcut working directory.
   Include a clean install/release-policy check and an update preserving existing data.

Focused automated coverage includes layout/retired CLI switches, no-write discovery,
renamed/copied folders with matching IDs, source preservation, PB cache/storage
isolation, unavailable versions, local backup recovery, first-save cancellation,
retry/departure failures and full native memory/RNG comparisons after Resume and
subsequent race updates. Retain the multiplayer setup/report/championship matrix
and Hall independence tests. Use [verification by change](../development.md#choose-verification-by-change)
and CMake/CTest; original-code comparisons stay in separate test executables.

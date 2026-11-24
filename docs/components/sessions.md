# Sessions and persistence

[app/session/](../../app/session/) owns mode/menu/race handoffs;
[app/menus/](../../app/menus/) owns menu transitions and
[app/persistence/](../../app/persistence/) owns codecs and storage.
[Playing](../playing.md) documents the user-facing routes and recovery choices.

The [portable-data and multiplayer-save design](portable-player-data.md) is being
implemented in stages. Explicit-root paths, host ownership and self-contained
career folders are in place; default storage/migration and native multiplayer
saves remain pending. The contracts below describe the current build.

## Transitions

Original and Responsive navigation share progression handlers. Back or revisiting
a prepared preview must not repeat race preparation, RNG calls, save encoding,
scoring or awards. Reports commit once; career Back suspends without acknowledging
them. Multiplayer reports retain their forward progression. Season-ending screens
retain the completed division/league context after gameplay progression changes it.

Require fresh Confirm/Back across transitions and release held input after
overlays or focus changes. Text entry owns its keys. Host Leave Race selects
the normal quit outcome immediately; original recorded/controller quit inputs
retain their timing gates. Computer Link remains disabled and deferred.

## Careers and records

- A career has an independent random identity; driver names can be duplicated.
  Setup remains a draft until the first durable checkpoint succeeds.
- Continue opens the career main menu. Resume Season restores a separate season
  checkpoint and RNG state; Practice cannot advance or replace it.
- An unfinished race retains its pre-race matchup. At a terminal finish,
  `RaceScene::copyLiveExit` stages exit/scoring on a private copy so restoring
  the pending finish/report cannot score twice.
- Capture/restore must not invoke gameplay handlers or the legacy save encoder,
  which consumes RNG. Validate the complete checkpoint before applying it;
  preserve supported older versions and reject nested suspended seasons.
- The installation-wide Hall archive receives newly accepted human laps from
  Practice, career races and local multiplayer, independently of private PBs.
  Three-lap entries require three observed completed competitive laps. Legacy
  winning banks and restored files never seed it. The career-menu browser
  displays the rankings and lets the player select a retained lap ghost for an
  isolated Practice challenge. See [shared Hall](shared-hall-of-fame.md).
- Personal-best ghost files belong to a career, track and Normal/Super league.
  Practice and career races share the target while Hall eligibility stays
  independent. The serial worker also owns their atomic writes and recovery;
  see [ghost recording and storage](personal-best-ghosts.md#saved-files-and-player-integration).

[Enhanced timing](enhanced-timing.md#session-delivery-and-ghost-targets) now delivers
the same live completions to separate PB/Hall storage, with conservative confidence,
independent failures and category-specific targets. Pinned Enhanced challenges use
the same isolated career continuation. Settings select timing and the Hall's
default category. Pending-finish checkpoints may carry optional Enhanced
display values in version 3 alongside their Original bitmap. Version 4 also
retains the category-specific Enhanced record notice through pending finishes
and record-page resumes; versions 1–3 remain readable. Normal checkpoints
continue to use version 2.

## Storage

Private career/index files use bounded, checksummed, versioned envelopes. A serial
worker writes immutable jobs to a flushed temporary file, atomically replaces
the head and retains two valid recovery generations. Ignore stale temporary
files, preserve unknown formats and validate checkpoint bodies during recovery.
Deletion drains writes before removing all generations. Saved means worker
completion, not job submission.

Explicit `--data-dir` launches use `careers/<label>/career.scr-career` and private
`personal-bests/original/` and `personal-bests/enhanced/` trees inside that folder.
`CareerFolders` discovers immediate child folders by their stored identities;
names, suffixes and paths do not identify a career. It preserves unavailable
formats and rejects directory links. Its cache is rebuildable; copy/rename with
the game closed. Legacy default launches retain flat career/PB files.

Duplicate identities require Replace / Keep Both / Cancel before load or autosave.
The serial worker stages complete folder transactions under `.transactions/`,
recovers them before discovery and retains replaced versions inside the winner's
`recovery/replaced/` tree. Keep Both rekeys supported owner-bound metadata and
every recovery generation, preserving checkpoint bytes, times and RNG. Unknown
formats block rekeying. Folder deletion uses the same worker/transaction boundary;
close waits for completion. Career operations do not restore or change the Hall.

Portable last-used IDs are separate `last-career`/`last-multiplayer` preferences
in version-11 settings, with serialized read/merge/atomic writes shared by display
and selection updates. No on-disk catalogue is needed. Legacy flat storage keeps
`last.scr-index`; older settings/checkpoint readers remain supported.

Write failure retains the latest RAM state and last good disk copy. Retry at safe
boundaries; switching careers and closing must drain writes or offer an explicit
unsaved exit/cancel. Never silently reset damaged or unsupported progress.
The Windows host acquires a process-held data-root file lock before constructing
the launcher, and releases it after session workers stop. It prevents competing
player writers across Windows accounts/logon sessions and resolves directory
aliases through their filesystem identity.
The separate shared Hall worker merges against the latest index under a short
cross-account file lock. Its immutable objects and recovery indexes are independent
of career deletion/PB replacement; both workers participate in departure checks.

Multiplayer manual saves retain the original thirty-slot codec and separate
atomic file replacement. Stage decoding on a private copy before committing;
prepare a save transaction once because its encoder advances RNG. Native careers
do not import manual-slot saves, and legacy HALL transport is disabled in the player.

See [player paths](../playing.md#data-and-recovery),
[verification](../development.md#choose-verification-by-change) and
[the checkpoint decision](../adr/0001-native-career-checkpoints.md).

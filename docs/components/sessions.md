# Sessions and persistence

[app/session/](../../app/session/) owns mode/menu/race handoffs;
[app/menus/](../../app/menus/) owns menu transitions and
[app/persistence/](../../app/persistence/) owns codecs and storage.
[Playing](../playing.md) documents the user-facing routes and recovery choices.

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
- Shared normal/Super Hall banks are authoritative across careers and multiplayer.
  Only better completed competitive human times replace records; ties retain the
  holder. Practice and opponents cannot add records. Restoring or deleting a
  career cannot roll them back.

## Storage

Career/Hall/index files use bounded, checksummed, versioned envelopes. A serial
worker writes immutable jobs to a flushed temporary file, atomically replaces
the head and retains two valid recovery generations. Ignore stale temporary
files, preserve unknown formats and validate checkpoint bodies during recovery.
Deletion drains writes before removing all generations. Saved means worker
completion, not job submission.

Write failure retains the latest RAM state and last good disk copy. Retry at safe
boundaries; switching careers and closing must drain writes or offer an explicit
unsaved exit/cancel. Never silently reset damaged or unsupported progress.
A per-data-directory mutex prevents competing player writers.

Multiplayer manual saves retain the original thirty-slot codec and separate
atomic file replacement. Stage decoding on a private copy before committing;
prepare a save transaction once because its encoder advances RNG. Native careers
do not import manual-slot saves, and legacy HALL transport is disabled in the player.

See [player paths](../playing.md#data-and-recovery),
[verification](../development.md#choose-verification-by-change) and
[the checkpoint decision](../adr/0001-native-career-checkpoints.md).

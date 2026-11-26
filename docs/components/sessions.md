# Sessions and persistence

[app/session/](../../app/session/) owns mode/menu/race handoffs;
[app/menus/](../../app/menus/) owns menu transitions and
[app/persistence/](../../app/persistence/) owns codecs and storage.
[Playing](../playing.md) documents the user-facing routes and recovery choices.

The [portable-data and multiplayer-save design](portable-player-data.md) is being
implemented in stages. Explicit-root paths, host ownership and self-contained
career folders and named multiplayer games are in place. Multiplayer uses native
autosave/Resume on default and explicit roots. Normal launches now use `data/`
beside the executable, with convention-based saved-game discovery. All six stages
are complete, with automated verification and maintainer manual sign-off.

## Transitions

Original and Responsive navigation share progression handlers. Back or revisiting
a prepared preview must not repeat race preparation, RNG calls, save encoding,
scoring or awards. Reports commit once; career Back suspends without acknowledging
them. Multiplayer Back returns home without acknowledging the saved report. Season-ending screens
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

[Native multiplayer checkpoints](multiplayer-checkpoints.md) retain full memory,
both RNG stores, host report/track-end flags, prepared matchups and optional finish/
record displays. A private terminal copy commits exit/scoring once. Restoring an
unfinished race returns to its retained pre-race matchup; restoring a report does
not acknowledge it or deliver old Hall attempts. The version-1 saved-game codec
adds independent host metadata. New Game selects title/division/league before
roster entry and requires a durable first save after roster confirmation. Continue
opens home without acknowledging the checkpoint. Resume Championship restores it;
Practise uses a temporary world. All modes retain their original progression rules.

Career and private PB files use bounded, checksummed, versioned envelopes. A serial
worker writes immutable jobs to a flushed temporary file, atomically replaces
the head and retains two valid recovery generations. Ignore stale temporary
files, preserve unknown formats and validate checkpoint bodies during recovery.
Saved means worker completion, not job submission.

All player launches discover `careers/<label>/career.scr-career` and
`multiplayer/<label>/game.scr-multiplayer` in immediate child folders. Each career
owns `personal-bests/original/` and `personal-bests/enhanced/`, including recovery
files. `SavedGameFolders` and `NativeMultiplayerStore` share
discovery while keeping mode-specific validation. Missing heads can use local
backups; unsupported/damaged data is preserved. Links and nested histories are
not followed. A first atomic checkpoint makes a new folder discoverable.

The folder identifies the selected game copy. Internal IDs still bind private
contents, but matching IDs in separate folders require no conflict resolution or
rekeying. Writes keep their selected immutable paths. Switching career folders
clears both PB caches so records cannot leak between copies. Browse selects a
game directly, with no management actions or technical folder labels. Copy/rename/
replace/delete complete folders externally with the game closed; no save-data
import/conversion or folder transaction service exists.

Last-used folder names are separate `last-career-folder`/`last-multiplayer-folder`
preferences with coordinated read/merge/atomic writes shared by display and
selection updates. No on-disk catalogue is needed. A renamed/missing preference
does not hide saves or choose another matching ID. Original save-disc codecs
remain isolated verification interfaces, unreachable from the player.

Write failure retains the latest RAM state and last good disk copy. Retry at safe
boundaries; switching careers and closing must drain writes or offer an explicit
unsaved exit/cancel. Continue Without Saving retains the current RAM checkpoint,
including Resume and temporary Practice; Leave Unsaved drops that retained game
before switching so a later Continue reads the last durable copy. Never silently
reset damaged or unsupported progress.
The Windows host acquires a process-held data-root file lock before constructing
the launcher, and releases it after session workers stop. It prevents competing
player writers across Windows accounts/logon sessions and resolves directory
aliases through their filesystem identity.
The separate shared Hall worker merges against the latest index under a short
cross-account file lock. Its immutable objects and recovery indexes are independent
of career deletion/PB replacement; both workers participate in departure checks.

The player no longer enters original manual multiplayer save/access/Replay flows.
The thirty-slot codec remains isolated for development/original-code tests. Native
saved games do not import it, and legacy HALL transport is disabled in the player.

See [player paths](../playing.md#data-and-recovery),
[verification](../development.md#choose-verification-by-change) and
[the checkpoint decision](../adr/0001-native-career-checkpoints.md).

# Shared Hall storage

Status: the player records new human lap/race completions into the durable shared
archive and displays it in the career-menu browser, with selectable lap ghosts
for isolated Practice challenges. Private
career PB files and ordinary Practice targets retain their existing format.

## Browser

`menus/hall_browser.*` owns filters, focus, two-page navigation and selected record
identity outside native RAM. `presentation/hall_browser_render.*` reuses the
original Hall's title/background artwork, 16-colour palette and seven-pixel font.
Its denser table retains the red name fields, grey time fields and blue markers.
The legacy renderer remains available to original-code tests and development
sessions; the enabled player has one shared Hall route, from an active career.
The old multiplayer Hall option is disabled in the player.

The header names All players and the active driver. Track, Normal/Super and
lap/three-lap selectors are stacked vertically above one board, with ten rows per
page and a centred page control at the bottom. Track cycling follows the original
Division 4-to-1 mapping (Little Ramp, Hump Back, Stepping Stones, Big Ramp, High
Jump, Roller Coaster, Ski Jump, Draw Bridge); archive keys remain native track IDs.
Competition ranks are derived across the whole board. The table contains rank,
name, original time
and verified ghost availability, with no result-history metadata. Race rows are
results only. Confirming an available lap row requests its exact ghost; the
contextual Race ghost label appears beside the page control for such a row.

Up/down traverses selectors, visible rows and Page. Left/right changes a filter
or page; confirmation advances the focused selector or page. Escape/controller B
returns to the same career. Number keys 1/2/3 focus selectors. There are no Reload
or Back buttons or permanent instruction line. The existing input clock provides
release gates and repeat timing in both navigation policies. Filters and selection
survive reopening; a fresh snapshot
follows the selected immutable ID, or clamps nearby if it was displaced.

Open and filter changes submit one coalesced read request to the existing
Hall worker. It publishes pending writes first, then reads a compatible index and
validates referenced ghosts. A failed write does not prevent viewing the last
valid committed archive. UI updates/rendering perform no file I/O; out-of-order
responses are ignored, and leaving cancels pending reads. The snapshot is stable
until refreshed, including when other game processes publish records.

Empty, loading and unavailable states are distinct. Missing/corrupt/incompatible
ghosts leave valid times visible as unavailable recordings. An unreadable archive
preserves its files and directs the player to reopen the Hall to retry; there is
no reset/delete action.
Changing filters, paging, loading and drawing do not change native RAM, earned
league, season progression or RNG. Browser position is transient UI state, not
part of career or shared record persistence.

## Practice challenges

The existing Hall worker re-reads the current board and validates the selected
immutable attempt ID before publishing a `PinnedHallGhost`. It copies only the
owner-neutral movement and retains the rival's name, original time and content/
rules identity. Removed rows or unusable objects refresh the browser without
launching another row's ghost. Back cancels a pending resolution; a later worker
response cannot launch it. There is no file read on the menu presentation thread.

`LapRecorder` chooses between an automatic career PB and an explicit pinned Hall
target. The latter is fixed at construction for the whole Practice session. Each
aligned player crossing starts a fresh playback epoch from that same recording;
PB improvements, clearing and archive eviction cannot replace it. Ordinary
Practice continues selecting the latest PB on each new lap. Both paths use the
same pure sampling, recovery cuts, finish fade and Draw Bridge guard.

`NativeGameSession` captures a full in-memory career/menu continuation before
configuring the challenge's track and complete native league context (0 or 11).
The normal preview/player/track/race initializers run on this temporary world.
Its menu is Practice-only. New private PBs and Hall laps use the actual temporary
track/league, while no Practice three-lap total can qualify as a race record.
The active career metadata and its suspended season remain unchanged.

Preview Back and race exit restore the full career memory, menu and preview
continuation before refreshing the remembered Hall board/ID. Saving or closing
during a challenge writes the existing normal career checkpoint and drains PB/
Hall writes; the temporary league, progression, preview RNG and race exit cannot
overwrite it. Challenges themselves are not resumed after restart.

The preview identifies rival/name/time/league in the native pixel font. The host
pause menu distinguishes the selected target from the career PB. Clear career PB
removes only the private record and suppresses that partial lap's PB eligibility;
Hall capture and the selected rival continue. Visibility and smoothing do not
reset the target. No Hall qualification/rank notifications are added to driving.

## Completion delivery

`LapRecorder` captures owner-neutral `LapRecording` data and delivers accepted
full laps independently of private PB filtering. It tracks an observed start independently
of its movement buffer: sample/bridge overflow yields an eligible time-only
attempt, while attaching during a partial lap cannot manufacture a full lap.
Clearing a private PB skips PB replacement for that partial lap, while Hall
capture continues. Private PBs still accept only strict improvements.

`HallRaceRecording` binds every newly delivered attempt to its actual native
roster name, track, league and accepted original time. It creates a fresh host
run identity using Windows randomness; SHA-256 of that identity, the crossing
sequence and category supplies a retry-stable 128-bit attempt ID. Timestamps and
gameplay RNG are not used. Every actual re-drive gets a new run identity.

The timing callback consumes the corrected `lapTimerHud` event snapshot, not the
already-cleared running clock. `state/human_record_adapter.h` reads the current
human name and cumulative time through a const recovered-layout boundary. Only
three consecutively observed full laps ending at counter 4 can produce a race
entry, and only in competitive play. Winning is not an extra condition. Practice,
league and local multiplayer all deliver individual laps; multiplayer has no
private career PB owner.

The session drains completions after each live step, before terminal checkpoint
handling. Restored PBs, native winning banks, checkpoints, finish acknowledgements
and manual-save encoders never mint attempts. The player no longer loads/writes
the old per-user `hall.scr-hall`, polls winning banks or offers its reset/recovery
flow. Old corrupt/future Hall files cannot seed or block careers or the new Hall.
Native working banks and their original routines remain for gameplay and oracle
contracts; archive reads/writes do not rewrite simulation RAM.

`app/records/hall_of_fame.*` owns 32 boards: eight tracks, Normal/Super and
lap/three-lap race. Each retains 20 attempts, including multiple attempts from one
normalised name. Matching ignores ASCII case and repeated/surrounding spaces;
display preserves capitalisation. Native driver characters and the 12-byte limit
remain the input boundary. Names and times never form a deduplication key.

Original packed-decimal times rank the attempts. Equal times share rank and keep
insertion order; a full board rejects a new cutoff tie. Retry-stable attempt IDs
prevent repeated submissions from adding a retained attempt twice. Callers must
never reuse an ID for different contents. A displaced attempt cannot re-enter a
board whose cutoff only improves. There are no delete/reset operations.

Lap time validation follows the native clock's minute hold at nine; three-lap
cumulative time can reach `29:59.97`. Empty tables have no entries, rather than a
sentinel time. The model test exercises the native cumulative routine at this bound.

`app/persistence/hall_table_codec.*` is a separate version-1 format with content/
rules identities, original times, attempt IDs, insertion order and optional ghost
digests. It does not decode legacy Hall or PB files. The index is bounded to
64 KiB/640 entries and rejects malformed ordering, IDs, BCD, references and counts.
A ghost reference records captured data; actual file availability is resolved
separately. No source-mode, date or control-preset history is stored.

## Objects, transactions and recovery

`app/persistence/hall_ghost_codec.*` stores owner-neutral presentation recordings,
bound to the attempt ID, board, original time and content/rules identities. It
shares sample/boundary types with private PBs through `race/ghost_lap_data.h`,
without changing the private PB format or recording behaviour. The Hall format
contains no career, source-mode or control history. Its checksum, counts, temporal
bounds and bridge references are validated before playback. The maximum encoded
recording is **2,494,844 bytes**: 65,536 samples and 64 bridge shapes of 510 words.

`app/persistence/shared_hall_store.*` provides a short synchronous transaction
(`SharedHallArchive`) and an asynchronous merge worker (`SharedHallStore`). An
index generation wraps the bounded table with a generation number and checksum;
the head is `index.scr-hall`, with `.bak1` and `.bak2` recovery copies. Objects
live under `ghosts/<32-hex-attempt-id>.scr-hghost`; SHA-256 digests are held in
the index. Filenames are derived from typed IDs, never stored paths.

Every transaction locks the shared root, loads the latest compatible valid
generation, and merges immutable attempts against it. Ghost objects are written
and flushed before the new index references them. Indexes use a flushed temporary
file and Windows replacement with write-through. Backup rotation preserves the
newest valid copy even when recovery starts from the second backup. Recovery
copies retain the two previous generations when available; the first generation
has no predecessor, and a single predecessor can fill both recovery slots.

A missing archive starts empty. An unreadable head recovers the newest valid
supported backup. No readable generation means the existing files are preserved
and writes fail. An unsupported format/content/rules identity in any index copy
blocks writes and collection. Access errors remain access failures; they do not
become a fresh or corrupt table. There is no delete/reset API or legacy import.

Missing, corrupt or unsupported objects affect ghost availability, independently
of valid times. A supplied healthy recording that cannot be published remains
pending for retry; it is never downgraded to time-only. Retrying the identical
attempt can repair its object. A pinned read owns a fully decoded immutable
recording and survives later eviction or collection.

Under the same transaction lock, collection preserves every object referenced by
the head and both valid recovery copies. It removes only recognised unreferenced
object/temporary names, leaving unrelated files alone. An unreadable index copy
prevents collection until a successful recovery rotation replaces it. Orphans
from an interrupted writer are also collected before creating another batch.
Three full index generations can reference at most **960** ghosts; a transaction
can add at most **320** more before publication/collection. External damage or
denied cleanup prevents a successful transaction rather than guaranteeing a
physical disk quota.

## Worker ownership and backpressure

The worker coalesces pending attempts to the best 20 per board, preserving prior
tie order. Twenty earlier equal/faster pending attempts guarantee that a dropped
candidate cannot qualify after those attempts merge. IDs, names and times are
never rewritten during a retry. Concurrent processes merge against the latest
index while holding the file lock, rather than writing stale local snapshots.

The pending set is bounded to 640 attempts. The general encoded-submission API
defaults to **64 MiB of payload plus entry accounting**, and returns `Full` when
its budget cannot accept another qualifying attempt. The caller must retain its
original submission until ownership is accepted; `Full` does not mean saved.

Live captures use the same worker and queue, passing immutable movement data
without encoding on the driving loop. Their lazy budget is derived from all 320
possible pending lap recordings at the existing sample/bridge allocation limits,
plus 640 entry allowances (`hallCapturePendingMaximumBytes`). This admits every
valid pending top-20 candidate without an additional producer queue or dropping
healthy recordings on failure. The ceiling is not reserved up front. An in-flight
snapshot can retain one further batch while newer attempts arrive, so the bound
is twice that budget plus container overhead, one active capture and codec/I/O
scratch. The writer encodes/hashes captures, then re-encodes only retained objects
one at a time to avoid a second entire encoded batch in memory.

`flush()` returns true only after acknowledgement of all queued attempts. Failure
keeps the pending data, exposes an error and waits for `retry()`; it does not
busy-loop. An acknowledgement after head publication is retry-safe even if the
process previously stopped before receiving it. Callers must handle a failed
flush before normal departure; destruction cannot rescue uncommitted data after
an unresolved failure. Process interruption proofs do not establish arbitrary
hardware/power-loss durability.

`app/platform/windows/shared_hall_storage.*` resolves installation identity from
the canonical installation directory, independent of user and EXE contents. Its
default location is under common Windows application data, with an injectable
common root for tests. It creates missing Hall directories with inherited local
Users modification rights and never re-permissions existing ancestors. Its bounded
byte-range transaction lock prevents replacement of an active lock file and is
released by process exit.

Normal player launches now use that common root. An explicit `--data-dir` keeps
the Hall under its isolated `shared-hall/` subtree. `enableCareers(privateRoot,
sharedRoot)` also supports separate private roots using one injected archive.
Directory creation and initial validation happen on the existing writer. Hall
failures share the safe-boundary Retry/Continue/unsaved-departure flow with career
writes, never a new notice during driving. Closing drains both stores.

The foundation proof tests competing read/merge/write processes, retry IDs,
publication and lock release after an owned child exits abruptly. The durable
store proof runs two actual store workers with stale initial observations and
terminates owned writers at seven flush/publication/collection boundaries.
Real cross-account evidence must come from distinct process-user SIDs, not merely
two processes or an ACL inspection.

Run `scripts/check_native_hall.py` after loading `scripts/env.ps1`. Reports,
receipts and the second-account launcher stay under ignored
`analysis/hall-of-fame/foundations/`. The report explicitly remains
`pending_real_account_check` until the prepared launcher is run from a different
Windows account and `--collect-account-proof` validates its executable identity.
This creates no Windows accounts and requires no password to be shared with Codex.

Run `scripts/check_native_hall_store.py` for the durable store, Hall ghost codec,
private PB regression tests and competing/killed-process proofs. Its identified
reports remain under ignored `analysis/hall-of-fame/durable-store/`. The durable
proof uses disposable roots and does not itself test another Windows account or
real player data; the earlier distinct-account proof covers the shared path/ACL/
lock primitives. Keep those evidence scopes separate.

`scripts/check_native_laps.py --verify-original` also runs completion delivery
and player ingestion tests, including slower-than-PB laps, both overflow cases,
partial entry, repeated recovery timestamps, competitive totals, local drivers,
PB clearing/deletion, no legacy imports, write retry and full RAM/RNG isolation.
Authored captured-entry fixtures exercise accepted crossings rather than treating
old record bytes as newly earned times. Original CPU execution stays test-only.
It also runs `native_hall_challenge_tests`: both navigation policies, all sixteen
track/league challenge setups, preview cancel, race return, safe close/resume,
missing objects/cancelled resolution, pinned ownership after collection, rendered
earned-lap playback and full native memory/RNG isolation. Fresh native routes and
the separately identified captured-entry playback fixture have distinct scopes.

`scripts/check_native_game.py --verify-original --jobs 1` also identifies browser
model/session coverage: all 32 boards, both navigation policies, page-boundary
ties, real ghost validation, empty/error/reopen states, stable IDs, original Hall
art/palette and complete native RAM/RNG isolation. UI fixtures are explicitly
synthetic and stay in disposable test roots; they never seed player data.

Windows API contracts: [byte-range locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
and [access-entry inheritance](https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance).

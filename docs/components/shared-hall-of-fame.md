# Shared Hall storage

Status: ranking, bounded codecs and a durable Windows shared store are implemented
and exercised through isolated tests/proof tools. They are not connected to the
player. The existing Hall and career PB feature continue to run unchanged.

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

The pending set is bounded to 640 attempts and **64 MiB of payload plus entry
accounting**. One in-flight batch can retain another such set while new attempts
arrive; payload/accounting is thus bounded to 128 MiB, plus bounded codec/I/O
scratch and container overhead. `Full` explicitly leaves ownership with the
caller, which must retain and retry the attempt. It does not mean saved. The
recording factory encodes immutable data; file I/O and merging run on the worker.
Player completion/backpressure integration is a later stage.

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
released by process exit. No production launch path invokes these primitives yet.

The foundation proof tests competing read/merge/write processes, retry IDs,
publication and lock release after an owned child exits abruptly. The durable
store proof runs two actual store workers with stale initial observations and
terminates owned writers at seven flush/publication/collection boundaries.
Capture and player/browser/challenge integration remain later work.
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

Windows API contracts: [byte-range locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
and [access-entry inheritance](https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance).

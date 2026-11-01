# Shared Hall foundations

Status: ranking, bounded table codec and Windows storage primitives are implemented
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
A ghost reference records captured data; actual file availability must later be
resolved separately. No source-mode, date or control-preset history is stored.

`app/platform/windows/shared_hall_storage.*` resolves installation identity from
the canonical installation directory, independent of user and EXE contents. Its
default location is under common Windows application data, with an injectable
common root for tests. It creates missing Hall directories with inherited local
Users modification rights and never re-permissions existing ancestors. Its bounded
byte-range transaction lock prevents replacement of an active lock file and is
released by process exit. No production launch path invokes these primitives yet.

The development proof tests competing read/merge/write processes, retry IDs,
atomic publication and lock release after an owned child exits abruptly. This
is not the full durable store: worker integration, recovery generations, ghost
publication/collection, capture and browser/challenge flows remain later work.
Real cross-account evidence must come from distinct process-user SIDs, not merely
two processes or an ACL inspection.

Run `scripts/check_native_hall.py` after loading `scripts/env.ps1`. Reports,
receipts and the second-account launcher stay under ignored
`analysis/hall-of-fame/foundations/`. The report explicitly remains
`pending_real_account_check` until the prepared launcher is run from a different
Windows account and `--collect-account-proof` validates its executable identity.
This creates no Windows accounts and requires no password to be shared with Codex.

Windows API contracts: [byte-range locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
and [access-entry inheritance](https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance).

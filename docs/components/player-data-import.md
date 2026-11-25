# Player-data import and portable defaults

The player always resolves `data/` from its executable directory. An executable
rename, shortcut working directory or different Windows user does not select a
different collection. `--data-dir` replaces the complete root; there are no narrow
player asset/settings/save overrides or silent AppData fallback. The host takes
`PlayerDataOwnership` before any launcher writes and keeps it until workers stop.
An unwritable root fails with the option of moving the game to a writable folder.

## Choice and discovery

The first empty normal root offers Import copies or Start fresh if recognised old
data is found. Discovery checks only the current user's old AppData and the
current executable directory's identified ProgramData Hall. It never scans other
profiles or every installation hash. If the game has moved, choose its old
Original Hall folder explicitly; both category paths are shown for review.

`--import-from <root>` opens the same review, including on a populated collection.
`--import-hall-from <Original-Hall-folder>` supplies a separately identified
archive. The launcher also offers both folder pickers. Explicit developer roots
never discover real AppData; their own flat layout conversion requires an explicit
choice. Already populated folder layouts are treated as existing collections.

`migration.scr-import` is a bounded, versioned, checksummed decision and import
receipt. It is not a portable-mode flag or saved-game catalogue. Start fresh
records the decision without copying old data. Later launches, accounts and moved
copies read only the chosen root unless the player requests another import.
Content fingerprints make repeated imports idempotent, including after a copied
game has been resolved, renamed or deleted. There is no ongoing synchronization.

## Contents and validation

`PlayerDataImport` uses read-only file/codec access; it never constructs a source
storage worker, creates a source lock, recovers the original source or modifies
source permissions. Supported pending source journals are recovered on a private
copy before discovery, including parked game heads and partially published
collections. Unsupported source journals block import and remain intact.
Assets establish the compatible native baseline before checkpoint
validation. Import neither consumes gameplay RNG nor executes original CPU code.

| Input | Destination / treatment |
| --- | --- |
| Compatible asset pack and provenance | `assets/`, retaining usable destination assets |
| Supported display settings | `settings.ini`, including independent saved-game selections; existing settings are kept unless replacement is selected |
| Flat career head and recoveries | `careers/<label>/career.scr-career` with identical generation/checkpoint bytes |
| Original/Enhanced PB trees | Matching career's `personal-bests/original/` and `enhanced/`, including all recovery generations |
| Folder careers / native multiplayer | Whole self-contained folders, preserving internal identities and unknown evidence |
| `last.scr-index` | A supported last-career preference, even if no settings document remains |
| Both Hall categories | Complete compatible indexes, recovery generations and immutable recordings, without minting attempts |
| Damaged/future files | Preserved and reported; recoverable careers keep their valid backups; unavailable Hall archives stay under `migration-recovery/` rather than becoming live |
| Orphan PBs / malformed career names | Retained under `migration-recovery/`, with a report |
| Old manual multiplayer / per-user winning banks | Excluded; originals remain untouched |

Duplicate identities enter the existing Browse conflict flow: Replace / Keep Both
/ Cancel. Import does not pick by date, rekey implicitly or merge progress. A
populated Hall is kept by default. Explicit replacement retains its complete prior
archive under `migration-recovery/`; Hall identities are never rekeyed. Valid
indexes with unavailable ghost objects retain their times and unavailable objects;
all referenced objects across valid recovery indexes are checked and reported.

Unsupported current settings cannot be overwritten by Play. Restore original
defaults explicitly retains the old document before writing supported settings.

## Publication and recovery

`FolderTransactions::publishCollection` extends the shared transaction service
with a separate version-2 collection journal. Version-1 saved-game journals remain
supported. The stage contains complete managed root entries: assets, careers,
multiplayer, Hall categories, settings, migration recovery and the import receipt.
Only these fixed destinations are admitted. Counts, byte totals, paths, checksums
and directory links are checked; imports are bounded to 4,096 files and 2 GiB.

Validation/staging finishes before a journal is flushed. Publication parks each
existing entry, publishes the validated replacement, then commits and removes
only verified inert transaction work. Recovery handles a stop before/after every
entry publication and completes before launcher/session reads. The root lock
prevents another player from observing partial publication. Blocked cleanup keeps
Play unavailable until recovery succeeds. Preparation failures leave live entries
unchanged; unsupported journals are preserved and block recovery.

The launcher performs import work off the presentation thread, blocks Play during
it, and joins its worker on close. Closing does not abandon a partially published
collection. Close the old game before importing; a changed folder snapshot is
rejected rather than treated as a coherent copy.

## Evidence

`player_data_import_tests` checks source immutability, every Original/Enhanced PB
recovery across all tracks/leagues, byte-identical checkpoint migration, full
native/RNG Resume and subsequent driving updates, Hall integrity, duplicate
resolution, repeated import, fresh-start persistence, future/corrupt sources and
category junction rejection and copied-source recovery for both journal versions.
Fifteen child-process terminations cover prepared,
parked, published and committed collection boundaries. Real player launches check
executable-relative storage, another working directory, executable/folder rename,
move and an independently editable copy without the ADF.

The junction fixture and existing ownership deny-ACL tests require execution
outside the Codex filesystem sandbox, which changes those Windows behaviours.
Launcher tests retain normal/compact review, working and report images. Scoped
CTest logs and source/fixture/executable receipts live under
`analysis/portable-player-data/stage-5/`. A real second Windows account, physical
controller and separate clean machine remain final acceptance work, not claims
made by these automated checks.

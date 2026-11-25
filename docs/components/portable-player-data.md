# Portable player data and multiplayer saves

Status: design accepted on 23 November 2026. Stages 1–5 are implemented with
automated verification; a real second-Windows-account acceptance check remains
pending. Stage 6 final acceptance remains pending. Normal launches use the
executable's `data/`, with explicit copy-based import and native multiplayer
autosave/Resume. [Sessions](sessions.md) and
[player paths](../playing.md#data-and-recovery) describe the current build.
[ADR 0004](../adr/0004-portable-player-data.md) records the ownership decision.

The [portable-app request](https://github.com/craigshaw/StuntCarRacer/issues/1)
motivated a unified storage model. Portable storage will be the normal player
experience, with self-contained saved games and consistent continuation in both
single-player and local multiplayer.

## Agreed product behaviour

| Area | Accepted contract |
| --- | --- |
| Default location | One `data/` directory beside the executable; no portable-mode marker |
| Ownership | Everyone using the same game folder sees the same careers, settings and records, regardless of Windows account |
| Independent copies | Moving the game folder carries its data; copying it creates an independent collection, without ongoing links or synchronisation |
| Career portability | One folder contains a career's progress, Original/Enhanced personal bests, ghost recordings and recovery copies |
| Multiplayer | Named, persistent games with New Game, Continue and Browse Saved Games; autosave and Resume follow the career model |
| Multiplayer setup | All four divisions and Normal/Super League are selectable during New Game, independently of career progress |
| Legacy multiplayer | No compatibility, import or migration of the original thirty-slot multiplayer saves |
| Race interruption | Resume the current human driver's pre-race matchup; retain earlier completed turns |
| Completed progression | Retain pending finish/results/report stages and committed scoring without applying them twice |
| Records | The Hall is shared within the game folder and independent of restoring, replacing or deleting saved games |
| Discovery | A compatible copied game folder appears automatically; names and paths are labels, not persistent identities |
| Copy conflict | Preserve identity when copying; offer Replace, Keep Both or Cancel for another version of that identity; never merge diverged progress automatically |
| Durability | Retain bounded versioned formats, validation, atomic file writes, two valid recovery generations and explicit save-failure handling |

Everyone using the folder needs normal Windows access to it. An unwritable data
root produces an actionable launcher error, with moving the game to a writable
folder as the ordinary remedy. There is no silent AppData fallback. Development
and special installations retain an explicit data-root override.

## Folder convention

```text
Stunt Car Racer/
  Stunt Car Racer.exe
  LICENSE
  THIRD_PARTY_NOTICES.md
  data/
    assets/
      stunt-car-racer.scr-assets
      ... imported-pack provenance
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

Normal and explicit `--data-dir` launches implement the paths above.
[Copy-based import](player-data-import.md) converts recognised older layouts
through an explicit launcher choice, preserving source files.
The two Hall categories retain their
independent indexes, retained recordings and recovery generations. Version
numbers belong in file headers, rather than ordinary category directory names.

Each saved-game folder contains its own metadata and full internal random ID.
Its initial readable name is derived from the driver/game title with a short
disambiguating suffix. Sanitise Windows filenames, including reserved names and
characters accepted by the driver editor; allow duplicate display names. Folder
renaming does not rename a driver/game, change its identity or invalidate a save.
The suffix is cosmetic and is never the authority for identity or uniqueness.

Discover immediate child folders by their recognised checkpoint files and
validate before exposing a loadable entry. Do not recursively discover nested
recovery/staging folders or follow directory links outside the chosen root.
Missing or corrupt heads with usable backups remain discoverable. Unsupported
and damaged games are preserved and shown as unavailable, not silently omitted
or treated as empty folders available for reuse.

The directory contents are authoritative. Last-used career and multiplayer IDs
belong in settings as separate preferences; no central catalogue is required to
make a copied save visible. Any discovery cache or location hint is rebuildable.
Coordinate preference updates so a delayed settings write cannot erase a changed
last-used selection, or vice versa. A missing last-used game requires selection
of another game rather than silently opening a different identity.

Keep existing Original and Enhanced PB bodies separate, under their career.
Track/league filenames use stable keys independent of translated labels and
folder names. Recovery generations stay with the file they protect. Ordinary
play does not enable development recordings; explicit recording output remains
a separate developer option.

### What copying means

| Intended operation | Copy with the game closed |
| --- | --- |
| Ready-to-play installation | The whole game folder, including the already imported assets |
| All player data | `data/` |
| One career with its best laps and ghosts | Its complete folder under `careers/` |
| One multiplayer game | Its complete folder under `multiplayer/` |
| Complete Hall with retained rivals | `hall-of-fame/`, including both categories and all referenced ghost files |

Only copying the whole game/data folder includes both progress and the shared
Hall. Opening or importing a saved game does not submit its stored bests or
winning banks as newly earned Hall attempts. Multiplayer continues contributing
live eligible attempts without becoming a career PB owner. There is no implicit
Hall merge when copied saved games are discovered. Combining two populated Hall
archives is a separate import operation, outside the initial implementation.

To share one career or multiplayer game, the recipient supplies a compatible
asset pack; assets and machine display preferences do not belong to that saved
game. Existing content/rules compatibility checks remain in force. Local
checksums establish integrity, not authenticity or future online eligibility.

### Duplicate identities

Copy an incoming saved game to a distinct destination folder so both versions
exist until the game resolves the conflict. Filesystem overwrites performed
outside the game cannot be intercepted by an in-game prompt.

Discovery groups copies by full internal identity. Present both paths and their
validated progress/roster summaries; dates may inform the player but must not
choose a winner. Conflicted games cannot be opened or autosaved until resolved.

- **Replace:** use the selected incoming folder's complete saved-game contents,
  including its PBs and recovery history, for that identity. Preserve the prior
  valid local contents through a recoverable replacement transaction. The Hall
  is unaffected.
- **Keep Both:** create a fresh host-generated identity for the incoming game.
  Rebind all supported owner-bound career metadata, PBs, ghost files and their
  recovery generations; retain the actual game state, RNG, results and times.
  Do not change the original or mint Hall attempts.
- **Cancel:** preserve both folders unchanged and leave that conflict unresolved;
  other valid saved games remain available.

Reject identity-changing operations that would require rewriting unsupported
contents. Names may coincide even when identities differ; that is not a conflict.
No decision depends on an absolute path, directory suffix or wall-clock ordering.

## Autosave and continuation

### Careers

Retain existing checkpoint semantics: Continue opens the career home, Resume
Season restores its independent suspended season, and Practice/Hall challenges
cannot advance that season. Keep supported older checkpoint versions readable,
including pending Original/Enhanced finish displays and record notices.

Move the career and both PB categories together without converting their game
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
or run the next turn as a side effect. The exact home/menu presentation is to be
verified during stage 4, with the same functionality under both navigation modes.

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

Resolve the default root from the executable directory, independent of process
working directory, shortcut location and executable basename. Make `--data-dir`
the override for the entire layout, including assets, settings and both Hall
categories. Resolve an explicit relative CLI path against the caller's working
directory once, then use its canonical root consistently. Update development
helpers together; `--data-dir` now supplies the whole explicit layout. Individual
development file overrides still take precedence.

Take a process-held data-root lock before the launcher imports assets, saves
settings or constructs sessions. It must work across Windows accounts and logon
sessions, and treat aliases of the same directory as one root. The former
`Local\\` named mutex was not sufficient for the new shared ownership. Use a
Windows file-lock protocol with crash release; a leftover lock filename alone
does not mean a writer exists. A second launch activates the existing window
where possible, otherwise reports that the root is in use without writing.

Use the directory's normal access permissions, without restoring the old
ProgramData installation-hash routing or imposing new broad ACLs on a player's
chosen folder. Retain Hall short locks and immutable-object publication where
needed by its existing storage protocol and standalone verification tools.
Separate category failures must not corrupt the other category or progress.

Share durable-write infrastructure across careers and multiplayer while keeping
their codecs and validators mode-specific. Keep serialization on immutable copies
and background file work off presentation/update paths. Saved means durable
worker completion. Drain all writes before switching games, conflict resolution,
deletion, replacement or closing; failures retain the latest RAM state and last
valid disk copy with Retry / Leave Without Saving / Cancel choices.

Ordinary file writes retain flushed temporary replacement and two validated
recovery generations. Folder creation, Keep Both, Replace and migration need a
recoverable multi-file transaction: validate/stage first, publish only complete
folders, and recover interruptions before discovery. Do not assume Windows can
atomically replace a populated directory in one rename. Keep transaction state
within the chosen data root, outside discovered game folders; preserve the last
good contents until the replacement has been validated. Remove transient work
only after commit/recovery. Unsupported data is never overwritten by recovery.

Capture, validation, migration, discovery, rekeying and presentation must not
consume gameplay RNG or run emulated original code. Host identity generation
remains independent of gameplay randomness. Baseline/content identities continue
to identify compatible game data, not the installation path.

## Migration and compatibility

Offer a one-time, copy-based import of recognised existing data, with a fresh-start
choice. A new game folder must be able to start fresh; do not silently seed every
new copy from AppData. Existing sources remain intact, with no ongoing sync or
automatic import when a different Windows user later opens the same game folder.
Explicit developer data roots must not import real player data implicitly.

| Existing source | Planned treatment |
| --- | --- |
| Current user's `assets/` and `settings/display.ini` | Validate and copy; migrate supported settings into `settings.ini` |
| Flat `careers/<id>.scr-career` and recovery files | Group into self-contained folders; preserve identity and checkpoint versions |
| `ghosts/<id>/` and `ghosts-enhanced-v1/<id>/` | Move copies under the matching career, including valid recovery generations |
| `last.scr-index` | Read a supported selection into the new preferences; it is not a discovery catalogue |
| Current installation's ProgramData Hall and Enhanced sibling | Copy each complete compatible archive, its recovery indexes and all referenced recordings |
| Existing explicit developer root | Apply the same recognised-layout conversion only through an explicit migration action |
| `saves/stunt-car-racer.scr-save` | Excluded: no import, conversion or migration into native multiplayer games |

Validate the entire staged migration and publish it through the folder-transaction
protocol. Interrupted or repeated imports must be resumable/idempotent and must
not duplicate games or Hall attempts. Saved games in a populated destination
follow the identity-conflict rules; never overwrite them because they appear
older. Leave an existing Hall untouched unless the player explicitly chooses
complete archive replacement through a recoverable transaction. Hall archives
do not acquire new identities through Keep Both. Preserve and report
unsupported/corrupt files, including their recoverable copies.

Only the current user's private data and an explicitly identified installation
are candidates for automatic source discovery. Do not scan other users' profiles
or combine every hashed ProgramData archive. If the executable has already moved,
ask for the old source location rather than guessing which Hall belongs to it.
The retired per-user `hall.scr-hall` remains outside the new Hall's import sources;
restored winning banks and legacy HALL slots cannot seed live-attempt archives.

### Resolved multiplayer compatibility policy

Legacy thirty-slot multiplayer saves are outside the compatibility scope. Do not
build an importer, roster extractor, fallback save system or dedicated backup/
archive step for them. Migration ignores those files; it has no operation to
delete or rewrite them. Their incomplete track-access and four-championship state
does not need reconstructing. This decision does not relax the existing native
career, PB or Hall migration/validation contracts, or define compatibility policy
for future versions of the new native multiplayer format.

The old career/access-slot dependency is also removed. Division and league are
explicit New Game choices, with all combinations available. No native career
selector, unlock aggregation or original access-file support is needed for
multiplayer. Resume uses the saved multiplayer configuration, never the current
state or presence of another saved game. Both former compatibility gates are
resolved; the implementation stages below reflect these decisions.

## Current implementation and intended ownership

| Existing component | Required work |
| --- | --- |
| `app/launcher/native_launcher.*`, `app/main.cpp` | One complete resolved layout; lock before launcher writes; unified path override and recoverable startup/migration errors |
| `app/platform/windows/shared_hall_storage.*` | Remove normal installation-path hashing/ProgramData routing; separate portable directory preparation from the old cross-account ACL policy |
| `app/persistence/native_career_store.*` | Folder discovery and identity/path separation; career-local PB paths; shared durable operations and settings selection migration |
| `app/persistence/native_career_checkpoint.*` | Preserve career versions 1–4; extract reusable native-memory range handling without weakening career validation |
| `app/menus/native_menu_flow.*` | Complete multiplayer continuation, including race/track summary pages and `multiplayerTrackEnd`; safe home/Resume handoffs |
| `app/session/native_career_session.cpp`, `native_game_session.*` | Shared saved-game lifecycle/failure handling with mode-specific capture; multiplayer drafts, autosave and exact-once restoration |
| `app/session/native_multiplayer_state.*` | Keep original scoring/turn/track-selection routines authoritative for the selected division/league; expose the state needed by validated capture through proper adapters |
| `app/persistence/hall_archive_engine.h`, category stores | Retain distinct models/codecs and immutable ghosts while taking explicit category roots |
| `src/state/` | Reusable state adapters and checkpoint primitives; no application discovery/UI ownership |

The current career validator rejects nonzero multiplayer mode and does not allow
`RaceResults` or `TrackResults`. Its codec also omits `multiplayerTrackEnd`, though
the menu continuation already contains it. A multiplayer checkpoint therefore
needs an explicit format/validator; merely allowing the mode byte is insufficient.
Reuse range encoding and worker mechanics where helpful, not career-only rules.

## Implementation stages

Implement in dependency order. Keep intermediate work exercised through explicit
test roots until migration and the new player routes are ready. Each completed
stage records its scoped verification below.

### 1. Resolve one layout and protect its writers — implemented, account acceptance pending

Add a typed player-data layout and a Windows root-ownership adapter. Derive every
default path from one canonical root; make category roots explicit. Add the
cross-session process-held lock before any launcher writes. Exercise the layout
under an explicit `--data-dir` without switching the normal launch default yet.

Exit: executable/working-directory independence, path aliases, complete CLI
override, unwritable roots, two processes/accounts and crash-released locks pass
isolated tests. No real user data is touched by development checks.

Implemented foundations: `persistence/player_data_layout.*`,
`launcher/native_launch_options.cpp` and Windows `player_data_ownership.*` resolve
the whole explicit root and protect it before launcher initialization. The lock
uses filesystem directory identity for window activation and a process-held byte
lock released by exit/crash. Hall locations are explicit and portable directories
inherit normal permissions; path-only archive constructors retain legacy test/tool
semantics. Stage 1 kept normal launch defaults unchanged; stage 5 enables portable
defaults. Stage 2 adds folder careers
under explicit roots; stage 4 replaces player manual multiplayer with named games.

Automated checks cover CLI precedence/working-directory independence, aliases,
competing processes, stale filenames, forced process termination, deny-ACL roots,
inherited Hall ACLs and the real player's isolated startup/duplicate launch/close.
Career integration also checks state/RNG preservation across a save/reopen using
both explicit Hall roots.
The deny-ACL fixture requires a run outside the Codex filesystem sandbox, which
replaces those fixture permissions. A real second Windows account/logon-session
exercise remains pending; the automated suite does not claim that coverage.
The local receipt at `analysis/portable-player-data/stage-1/check-results.json`
identifies the checked sources, fixtures, test executables and matching staged
player. Its companion `ctest.log` records the seven focused Release suites.

### 2. Make careers self-contained and discoverable — implemented

Refactor storage addresses away from ID-derived global paths. Discover/validate
career folders, colocate both PB categories and recoveries, and store last-used
IDs as coordinated preferences. Point both Hall stores at their portable roots
without changing record models or object identities. Implement the reusable
folder transaction and duplicate-resolution service, initially for careers.
Extend ignore/source-policy coverage for new runtime file types and their
recovery/temporary files before generating local fixtures.

Exit: a copied/renamed folder works without catalogue edits; duplicate names are
independent; Replace/Keep Both/Cancel and interruption recovery preserve complete
career data. Supported older checkpoints still load, all rekeyed PB/recovery files
validate, and the Hall is unchanged by career operations.

`persistence/career_folders.*` discovers immediate career folders by stored
identity, including usable backups and unavailable damaged/future versions.
Portable checkpoints use `career.scr-career`; both PB categories, all recovery
generations and preserved damaged files travel inside the folder. Folder names
are labels and duplicate display names remain independent. Discovery is cached
in memory and rebuilt at startup; copy or rename folders with the game closed.
Legacy default launches and path-only test/tool constructors retain flat storage.

Version-11 settings coordinate `last-career` and `last-multiplayer` preferences
with display writes through `persistence/player_preferences.*`; existing display
versions remain readable. Missing preferences do not hide copied careers.

`persistence/folder_transactions.*` stages complete contents with a checksummed
journal and manifest under `.transactions/`, parks old folders, publishes the new
folder and commits before cleanup. Recovery completes a prepared transaction
before discovery. Unknown/damaged journals and directory links stop the operation
and preserve files. Incomplete staging without a journal is inert and retained.
Limits are 4,096 files and 2 GiB per transaction tree. This service also accepts
multiplayer folder paths for the later codec/UI stages.

Browse Careers exposes Replace / Keep Both / Cancel for duplicate identities,
defaulting to Cancel. Replace uses the explicitly selected complete copy and
retains the others under `recovery/replaced/` inside it. Keep Both assigns a new
host identity and rekeys supported career/PB/ghost generations without changing
checkpoint state or times; unknown owner-bound formats block that operation.
Conflicted identities cannot load or autosave. Copy resolution and deletion run
on the serial storage worker after pending writes drain; closing waits for them.
The shared Hall is independent of every career-folder operation.

`career_folder_tests` covers catalogue-free discovery, renamed/Unicode folders,
duplicate names/identities, both PB categories across every track/league, all
recovery generations, settings-write coordination and failure preservation.
`folder_transaction_tests` terminates child processes at eleven create/replace/
delete boundaries and checks restart recovery plus blocked I/O and unsafe journals.
`native_career_folder_session_tests` exercises Cancel/Replace/Keep Both and
deletion/close in both navigation modes, checks full native memory during conflict
UI, full native memory on Resume and through sixteen subsequent gameplay updates,
and captures eight menu images for visual inspection. Existing career/autosave, ghost, Enhanced,
launcher/display and Hall regressions cover compatibility.

The local receipt `analysis/portable-player-data/stage-2/check-results.json`
identifies sources, fixtures, test executables and the matching staged player;
companion CTest logs and menu images record the checked scope. No original-code
comparison or real controller/second-account acceptance is claimed by this stage.

### 3. Add complete native multiplayer checkpoints — implemented

Specify a bounded, versioned multiplayer envelope and validator. Reuse compatible
memory-delta primitives, with explicit mode/page/roster/turn/report constraints.
Inventory every original-memory and host-continuation field needed at each saved
boundary. Include report flags and optional finish displays; reject wrong-mode,
invalid or unsupported bodies before applying any state.

Exit: fresh-session round trips at every multiplayer boundary preserve memory,
both RNG stores and future progression for 2–8 humans. Consecutive resumed and
uninterrupted runs agree through both tracks, forfeits, bonus awards and the
four-championship reset. Existing career byte formats/validation remain compatible.

`persistence/native_multiplayer_checkpoint.*` implements distinct bounded,
checksummed version-1 checkpoint and saved-game envelopes. The latter carries host
identity/title/time/generation metadata independently of native progression.
`session/native_multiplayer_checkpoint_session.cpp` captures prepared matchups,
private terminal exit/scoring, pending finishes and committed reports, then restores
validated state without replaying progression or Hall delivery. The shared memory
delta primitive preserves existing career bytes. Stage 4 connects the foundation
to the player without changing the checkpoint format.

The [checkpoint contract](multiplayer-checkpoints.md) inventories native and host
state, supported boundaries, validation and finite comparison scope. New codec
tests cover 448 page/count/division/league round trips plus malformed/future data
and career format compatibility. Repeated fresh-session comparisons cover all 56
multiplayer configurations, both tracks, every human, preparation, interruption,
bonuses, F1 forfeits, all reports and fourth-championship reset. Finish/record
display and populated Hall isolation are checked under both navigation policies.
Scoped native/original CTest logs, eight reviewed images and matching staged-player
identities are retained at `analysis/portable-player-data/stage-3/`.

### 4. Integrate the multiplayer saved-game journey — implemented

Implement named-game setup with unrestricted division/league selection,
Continue/Browse, home/Resume, deletion and autosave using the shared lifecycle.
Preserve roster-confirmation draft semantics, pre-race interruption and terminal
private-copy capture. Extend duplicate rekeying and transaction handling to the
new format. Retire the slot/disc/format/name and access-file flows from the player;
retain isolated original-code tests without adding legacy multiplayer import.

Exit: both navigation policies, keyboard/controller, 2/8-player setup, new-save
failure, every turn/report resume, game switching, deletion and close/retry/cancel
work. Every division/league combination works without a career, and its saved
configuration survives copying and changes to other saved games. No scoring,
damage transfer, preparation, RNG advance or Hall completion is repeated by
reopening a saved stage. Inspect the new menus visually.

`NativeMultiplayerStore` uses the existing serial career actor and the extracted
`SavedGameFolders` service for discovery, reservations, conflicts and transactions.
Native game heads retain two valid recovery generations; Keep Both changes only
outer multiplayer identities in all supported generations. Replace archives every
other complete copy inside the winner. Windows extended paths cover nested recovery
histories. Settings updates preserve the independent career/multiplayer selections.

The player now offers title/configuration/roster setup, Continue/New/Browse,
home/Resume/Practise, copying and deletion in both navigation policies. Confirmed
setup requires its first durable save; failed setup supports Retry/Cancel. Safe
boundaries autosave, unfinished racing retains its prepared matchup, and pending
finishes/reports reopen without acknowledgement. Modal rendering and temporary
Practice preserve the saved competition. Departure failures offer Retry, Leave
Unsaved and Cancel; explicit Continue Without Saving retains RAM for Resume.
Legacy multiplayer files remain untouched and no longer provide player routes.

Storage and saved-session tests cover recoveries/future data, all division/league
choices with 2/8 humans, both copy choices, blocked writes, cancellation, deletion,
close, game switching, career deletion/restoration and four-championship resets.
Full native/RNG continuation comparisons remain in the stage-3 suites. Retained
images, scoped CTest results and source/fixture/executable identities live under
`analysis/portable-player-data/stage-4/`. Host input is synthetic; physical controller,
real second-account and clean-machine portability acceptance remain pending.

### 5. Deliver migration and enable portable defaults — implemented

Implement source discovery, explicit import/fresh-start choices and validated
transactional publication, including both Hall categories and all supported
recovery files. Exclude legacy multiplayer slot files from import and migration.
Handle a populated destination through the same conflict service. Update helpers
to one complete data root, migrating/removing the player's narrow legacy path
switches only after their callers are updated. Keep explicit recording options.
Switch normal launches to `<executable-directory>/data/` after these paths pass.

Exit: fresh install, existing install, moved old install, interrupted/repeated
import, incompatible sources, another Windows account and explicit developer
roots behave as specified. Originals remain untouched. A later launch reads only
the chosen root and does not silently repopulate it from old Windows locations.

`PlayerDataImport` supplies read-only source discovery/validation, grouping of flat
careers and both PB categories, native-folder copying, settings/selection migration
and complete Hall copying. Unknown or damaged files and orphan bests are retained
and reported. Populated destinations preserve existing games for the common
conflict service; Hall replacement requires an explicit category choice and keeps
the previous complete archive. Content receipts prevent duplicate imports.

The launcher offers review/folder selection, Import copies and Start fresh, then
performs import off-thread. Collection journals extend `FolderTransactions` while
keeping version-1 saved-game recovery compatible; recovery finishes under the root
lock before any launcher/session reads. Portable defaults now follow the executable
directory. The player exposes one data-root override and explicit import/recording
options; helpers no longer split player settings/assets/save paths or enable
recording implicitly. Clean distribution checks are separate from updating a
played-in installation, whose `data/` is preserved.

Automated checks cover all PB recoveries, exact native memory/RNG and subsequent
driving, fifteen collection process-interruption boundaries, future/corrupt sources,
junction rejection, repeat/fresh choices, existing-root conflicts and Hall integrity.
The real player is launched from another working directory, moved and renamed,
then copied into an independently editable collection without its ADF. Launcher
review/working/report images, CTest logs and matching source/fixture/executable
receipts live in `analysis/portable-player-data/stage-5/`. Actual second-account,
physical-controller and separate clean-machine acceptance remain stage 6 work.

### 6. Verify portability and stage the release — pending

Run the focused native and original-code regressions below, plus a clean-machine
copy/move exercise and interactive storage-failure checks. Update playing,
development, architecture and affected component guides to the implemented
contract, removing the pending notices only when justified.

Verify CMake Install preserves an existing player's `data/` when updating the
executable. Validate distributable contents in a separate clean install prefix:
the current release checker intentionally accepts only the EXE and notices, so
a played-in `dist/` is not the distribution input. Update release/helper checks
to maintain this distinction, without deleting or bundling player data.

Exit: CMake preset builds/CTest pass for their declared scope; the staged binary
matches the verified build; fresh source/fixture/executable receipts and visual
results live under `analysis/`. Generated saves/assets remain outside Git.

## Verification plan

Use [verification by change](../development.md#choose-verification-by-change).
Add focused layout/discovery, folder-transaction/migration and multiplayer-codec
tests to `tests/unit/` and `tests/integration/` as appropriate; register new targets
in `cmake/NativeTests.cmake`. Extend test-only original comparisons through
`cmake/OracleTests.cmake` where original progression is affected.

| Contract | Required cases / existing regression base |
| --- | --- |
| Portability | Move/copy to a different path/drive; rename EXE; launch via shortcut with another working directory; compatible imported assets remain usable without the ADF |
| Ownership | Same folder from two Windows accounts; two independent folders; alias paths; second launch; crash release; no cross-root writes |
| Career/PB compatibility | Older checkpoint versions, pending finishes, independent suspended season, Original/Enhanced PBs, clearing/deletion; `native_autosave_tests`, `native_career_session_tests`, `native_ghost_store_tests`, `enhanced_store_tests` |
| Folder operations | Duplicate names/IDs, wrong modes, missing/corrupt heads, unknown versions, identical and diverged copies, rekeyed recovery generations, interruptions at each transaction step |
| Multiplayer state | All player counts, turn/report boundaries, per-driver damage, F1 forfeits, all four divisions in Normal/Super, bonuses and fourth-championship reset; extend `native_multiplayer_session_tests` and new codec/resume tests |
| Multiplayer setup | All division/league combinations with no careers or legacy saves present; unchanged career progression; selected configuration survives Resume, copying and career deletion/restoration |
| Legacy exclusion | Legacy multiplayer files neither enable player load/import routes nor become native games during migration; no dedicated archive/backup operation |
| Failure lifecycle | First-save failure, failed PB/Hall categories, retry, switch/delete/close drains, Leave Without Saving/Cancel; extend `native_autosave_tests` and `native_navigation_tests` for both modes |
| Hall integrity | Original/Enhanced archive relocation with Ready ghosts, recoveries/collection, no duplicate attempts on Resume/import, no rollback on game replacement; `shared_hall_store_tests`, `shared_hall_storage_tests`, `native_hall_completion_tests`, `native_enhanced_session_tests` |
| Launcher/preferences | Whole-root override, settings/last-used coordination, unwritable root, import/fresh start, repeated migration; extend `native_launcher_tests` and `native_display_tests` |
| Original rules | Fresh matching comparisons for changed multiplayer/menu transitions at the same selected division/league; `native_multiplayer_state_tests`, `native_career_state_tests`, `native_menu_flow_tests` under `windows-oracle`; test unrestricted setup as the new native policy rather than claiming original access-menu equivalence |
| Packaging | Clean CMake Install prefix passes `scripts/check_release.py`; updating a played-in folder preserves `data/`; repository policy rejects generated saves/assets including new extensions |

Use one incremental `windows-release` build and focused CTest selection per
stage. Use `windows-oracle` only for affected original-code comparisons; Musashi
never enters the player. Add Debug checks when a change/failure warrants them,
rather than repeating every suite in both configurations. The final cross-cutting
acceptance includes `scripts/check_native_game.py --verify-original`, extended for
the new routes, and affected Hall/Enhanced/ghost checks invalidated by relocation.
Keep finite comparison scope and fixture identities explicit.

Storage and state changes require full simulation/RNG comparisons across capture,
restore and subsequent updates. New menu screenshots and manual input checks
cover both navigation modes; presentation remains at the existing clocks/views.
There is no change to 120 ms gameplay, 50 Hz wheel/engine state, race rules or
Computer Link support.

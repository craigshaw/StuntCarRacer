# Verification sources

- `unit/`: asset-free native checks and synthetic Python evidence/policy tests.
- `integration/`: the native game, startup, sessions, display, launcher and rendering.
- `oracle/`: comparisons against original 68000 code; built only with `SCR_BUILD_ORACLE`.
- `support/`: test-only namespaces, oracle code ranges, comparison projections and
  temporary-output helpers. Shared runtime adapters belong in `src/state/`.
- `replays/`: authored control sequences and their provenance, retained unchanged.

Use the CMake/CTest presets and [development guide](../docs/development.md) to
choose checks. Private fixtures and retained receipts stay at their existing
`analysis/` paths; disposable test output belongs under `build/`.

`career_folder_tests` and `native_multiplayer_store_tests` cover convention-based
discovery, copied/renamed folders with matching internal IDs, independent writes,
PB ownership, local backup recovery and source preservation. The career folder
session suite compares full native memory/RNG and following race updates after
selecting a copied career directly. The multiplayer saved-session suite retains
its setup/report/championship and failure matrix with direct folder selection.
There is no legacy-layout conversion or file-management feature to exercise.
Final physical-controller, second-account, other-drive and clean-machine acceptance remains
separate from automated checks; see the [plan](../docs/components/portable-player-data.md).

`player_data_ownership_tests` launches the real player through copied, moved and
renamed installations, including a real Windows shortcut with a different working
directory. An optional populated convention-format fixture also exercises asset
loading and game entry while checking every saved-data byte is preserved. The
game wrapper's `--verify-portable` pass supplies generated careers, multiplayer,
Original/Enhanced PBs and both Halls, retaining fixture/executable identities and
an isolated copy for hands-on tests. Cross-process alias/crash/ACL checks do not
substitute for a real second Windows account or physical controller.

`native_multiplayer_checkpoint_tests` validates the asset-free multiplayer format
and unchanged career bytes. `native_multiplayer_checkpoint_session_tests` compares
all player counts/divisions/leagues across repeated fresh-session restores, both
tracks, reports, forfeits, bonuses and reset. See the [checkpoint contract](../docs/components/multiplayer-checkpoints.md)
for the distinction between natural race finishes and controlled bonus/record
fixtures. `native_multiplayer_store_tests` checks shared-worker durability,
folder discovery, independent copies and failed-write retries.
`native_multiplayer_saved_session_tests` covers named-game setup, autosave/Resume,
failures/departures, copied-folder selection, independent careers/games and four-championship
completion in both navigation policies. A resumed pre-race checkpoint must stay
in its selected folder without creating another saved game. It uses synthetic
host input rather than physical controller acceptance; its Enhanced record notice
is a controlled display fixture. Rendered menus and source/fixture/executable
receipts stay under `analysis/`.

Hall coverage separates pure ranking/codec tests, the Windows merge/recovery
store, and accepted-completion delivery. `hall_completion_tests` drives the native
timing routine through real accepted events, including overflow and partial-entry
boundaries. `native_hall_completion_tests` exercises player ingestion with authored
captured-entry trajectories, private PB independence, local drivers, failed writes
and full native memory/RNG isolation. Injected old record banks are tested only
as excluded imports, never described as newly driven completions. See the
[shared Hall contract](../docs/components/shared-hall-of-fame.md).

`hall_browser_tests` exercises UI navigation and identity retention without assets.
`native_hall_browser_tests` loads isolated synthetic boards through the actual
career-menu route, verifies ghost files on the Hall worker, and retains previews
for visual review. Both navigation policies, all 32 boards, page-boundary ranks,
empty/error states and native RAM/RNG isolation are covered. The original Hall
renderer still has independent pixel comparisons in `oracle/`.

`native_hall_challenge_tests` covers pinned targets, every track/league challenge
setup under both navigation policies, preview cancel/race return, safe career
close/resume and missing/cancelled resolution. An explicitly captured-entry
earned-lap fixture checks visible playback, PB clear/re-record, archive collection
and native memory/RNG isolation; it is not a fresh-start completed-lap claim.

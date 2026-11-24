# Native multiplayer checkpoints

Status: the stage-3 codec and session capture/restore foundation are implemented.
Named-game menus, durable autosave, folder discovery and departure/failure handling
are stage 4 of the [portable-data plan](portable-player-data.md). The player still
uses its existing multiplayer journey; this foundation has no legacy-slot importer.

`app/persistence/native_multiplayer_checkpoint.*` owns validation and encoding.
`app/session/native_multiplayer_checkpoint_session.cpp` captures and restores the
session without calling a legacy save encoder. Original race, scoring, bonus and
championship-reset routines remain unchanged.

## Format and identity

The version-1 checkpoint envelope has a distinct multiplayer magic, bounded byte
length and checksum. Its body binds the verified asset-baseline identity and stores
ordered changed-byte runs against that baseline, preserving the entire 512 KiB
native memory. The shared primitives in `src/state/native_checkpoint_io.h` retain
the previous career encoding exactly. The two verified compatible ADF baselines
use the same existing compatibility identity; arbitrary altered baselines fail.

A separate version-1 saved-game envelope is ready for `game.scr-multiplayer`. It
adds a full 32-character lowercase hexadecimal host identity, printable nonblank
title of up to 32 ASCII characters, last-played timestamp and nonzero generation.
Its checksum covers metadata and the checkpoint. Changing this host identity does
not rewrite native driver names, memory or progression. Neither codec creates IDs
or consumes gameplay randomness. Folder publication and generation rotation remain
the storage worker's stage-4 responsibility.

Checkpoints are limited to 4 MiB including their header; the complete saved-game
envelope allows an additional 256 bytes. Lengths, delta ranges, flags, names,
selections, roster/standings permutations, per-driver damage, configuration,
turn/report relationships and optional display values are checked before use.
Future format/baseline/timing-method versions are classified as unsupported.
Wrong modes, unsafe pages, corrupt checksums, overlapping/out-of-range runs,
truncation and trailing data fail validation. Checksums establish local integrity,
not authenticity.

## State inventory

All native bytes are retained, rather than selecting only the fields below. These
are the continuation-sensitive owners that must survive any future refactor.

| Owner | Retained state |
| --- | --- |
| Roster and configuration | Additional-player count at `0x5eb76`; active human IDs `11` downwards; names at `0x1ecab + 16 * id`; division/league at `0x1c9ce`/`0x1c9d0`; native division-track and league-parameter tables |
| Human turn and pace car | `0x5eb79`, fixture IDs at `0x1ca27`–`0x1ca29`, pace-car identities/names, track selector `0x5eb77`, human count `0x5eb78`, current turn `0x1ca35` and race ordinal `0x5eb7b` |
| Controls and damage | Per-human five-key bindings at `0x5fb3a`, current bindings at `0x60c88`, per-human damage at `0x1ca36`, shared allowance/exit backup at `0x1c9cf`/`0x1bbb5` |
| Competition results | Roster at `0x1c9c2`, wins/places/races and standings through `0x1ca1a`, all three 24-slot time planes at `0x1c908`/`0x1c920`/`0x1c938`, lap/race bonus holders at `0x1ca23`/`0x1ca24` |
| Accumulation and reports | Championship counter `0x57c67`, completed-race word `0x57c72`, native report state `0x57c62`, track-end flag `0x5eb7a`, summary flag `0x1bbcb` |
| Randomness | Current five-byte store `0x625a2`–`0x625a6` and saved/overlapping store `0x636b8`–`0x636bb` |
| Host menu continuation | Exact page, competitive flag, division/league context, `multiplayerTrackEnd` and remembered menu selections |
| Host preparation | Prepared track/league and `previewPrepared`; native overview geometry, rotation and post-preparation RNG are already in the memory delta |
| Host finish/record display | Pending-finish flag, fixed Original 320×200 RGB bitmap, optional Enhanced clock values and optional Enhanced lap/race record notice |

The standings bounds in shared native UI scratch are cleared during overview
preparation and rebuilt by scoring. A prepared matchup can therefore legitimately
carry cleared bounds; a report requires the complete active-driver bounds. These
scratch fields are not alternate roster identities.

Hall run IDs, accepted-completion queues and storage-worker deliveries are not
restored. Displaying a saved finish or record notice never offers its old times as
new attempts. Private career PB ownership is absent from multiplayer continuation.
Host input clocks, fades, live race objects and fractional driving time are reset;
an active race is resumed from its prepared matchup.

## Capture and restore boundaries

`NativeGameSession::captureMultiplayerCheckpoint()` enables native checkpoint
retention after confirmed setup. `restoreMultiplayerCheckpoint(bytes, held)`
validates on private memory and constructs a validated continuation before
replacing the active session. Rejection leaves the active state intact. Outstanding
folder work must finish and existing stores must flush before replacement. Held
host input is blocked until released. The adapter retains the existing memory
object because native input/name adapters refer to it.

| Captured boundary | Resume contract |
| --- | --- |
| Main after confirmed setup or a championship | Retain roster, selected configuration and accumulated results; starting the next championship follows the existing rules |
| Matchup before overview preparation | Keep the exact human, track and RNG; preparation happens once on entering the overview |
| Prepared/rotated overview | Normalize the saved page to Matchup while retaining preparation; re-entering the overview reloads it without repeating initialization or RNG restoration |
| Unfinished race | Return the immutable prepared matchup retained immediately before race initialization; keep all earlier humans' completed turns and discard the current partial race |
| Terminal race before acknowledgement | Copy native exit/damage handling and `NativeMenuFlow::returnFromRace()` onto private memory; save the resulting Results stage, with its scoring already committed |
| Natural pending finish | Retain its Original/Enhanced presentation in front of committed Results; Confirm reveals Results without running exit/scoring again |
| Results, Records, RoundResults or RaceResults | Retain the pending report and apply only its next transition on acknowledgement |
| TrackResults or track-end Standings | Bonuses are already committed; retain `multiplayerTrackEnd` so acknowledgement advances the track/championship once |

Only Main, Matchup, Results, Records, RoundResults, Standings, RaceResults and
TrackResults are serialized menu pages. Race/Preview, single-player season-ending
pages, name entry and legacy save-disc pages are not accepted checkpoint pages.
Direct capture during an unsettled transition is rejected. In normal play the
stage-4 lifecycle will capture these boundaries automatically; callers must capture
setup before entering the first race. Existing player launches do not enable this
retention merely by choosing multiplayer.

## Verification

`native_multiplayer_checkpoint_tests` is asset-free. Its 448 round trips cover
2–8 humans, all four divisions, both leagues and every serialized page. It checks
complete native memory, fragmented deltas, metadata/rekey independence, malformed
and future data, optional displays, and the unchanged career version-1–4 format.

`native_multiplayer_checkpoint_session_tests` compares independently evolving
uninterrupted and repeatedly reopened sessions. All 56 player-count/division/league
combinations run both tracks with all humans; the suite covers both navigation
policies, prepared/rotated previews, current-race interruption, private terminal
capture, all reports, F1 forfeits and the fourth-championship reset. Comparisons
include full native bytes/RNG at safe boundaries and terminal simulation exits.
Neutral host input is sampled on both sides before comparing report input scratch.

The suite uses a natural pace-car finish for real pending-finish capture and checks
Original/Enhanced pixels after restore. Nonzero bonus holders and Enhanced record
notices are controlled fixtures, not claims of newly driven human records. Eight
retained display images cover both policies. Populated Original and Enhanced Hall
archives must keep identical files/generations through restore and notice display.

Existing multiplayer, career/autosave, portable-career and Enhanced UI suites guard
the current player routes. Focused original-code comparisons use
`native_multiplayer_state_tests`, `native_menu_flow_tests` and
`native_career_state_tests` in `windows-oracle`; Musashi remains test-only.
Scoped source/fixture/executable identities and CTest logs live in
`analysis/portable-player-data/stage-3/`.

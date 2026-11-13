# Enhanced timing implementation plan

Status: stages 1–6 complete. Crossing estimation, independent capture/storage,
dual live delivery, category-specific targets, settings and timer/Hall UI are
implemented. Final release verification and staging are recorded under
`analysis/enhanced-release/`, with the evidence boundaries below.

Add millisecond lap and three-lap timing using interpolated crossings on the
existing 120 ms simulation timeline. Keep Original timing and its records
available independently. Enhanced timing becomes the default display and record
category, while the original simulation, race-winner rules and championship
progression remain authoritative for gameplay.

## Agreed behaviour

| Area | Contract |
| --- | --- |
| Enhanced result | Elapsed simulation time between estimated crossings, plus 260 ms per recovery |
| Precision | Store integer microseconds; display and rank in milliseconds as `m:ss.mmm` |
| Original result | Preserve the accepted packed-decimal time, its correction and ranking rules |
| Timing mode | Original / Enhanced setting, independent of smoothing, view and driving controls; default Enhanced |
| Capture | Capture both results for every observed eligible human lap, regardless of the selected mode |
| Records | Separate PBs, Hall boards and associated ghost targets for each timing category |
| Ties | Equal displayed milliseconds share rank; a PB tie retains the existing target |
| Uncertain estimates | Show an approximate Enhanced result; exclude it from Enhanced PBs and Hall rankings |
| Race rules | Preserve native crossing eligibility, winner decisions, AI, points and progression |

Apply the current mode boundaries: Practice and competitive human laps can
qualify for the Hall; three-lap totals require competitive play and three
consecutively observed full laps. Private PBs belong to a career and remain
available from Practice and career league racing. Local multiplayer contributes
Hall attempts without acquiring a private career PB owner.

## Existing foundations

The [original timing routine](../../src/race/race_timing.cpp) adds 13 BCD
hundredths per timing update, applies a further 0–12 hundredths at applicable
completed crossings, and clears the running clock after capturing the result.
The [race adapter](../../src/state/native_race.h) executes two additional timing
updates inside recovery. These original behaviours remain unchanged.

[RaceScene](../../app/race/race_scene.cpp) supplies movement observations on the
120 ms presentation timeline. [LapRecorder](../../app/race/lap_recording.cpp)
already aligns ghost boundaries between adjacent forward progress samples.
[LapRecording](../../app/race/ghost_lap_data.h) stores microsecond movement
timestamps separately from the original accepted time. Its existing boundary
fallbacks and saved durations do not certify an Enhanced result.

The [PB contract](personal-best-ghosts.md) and
[shared Hall contract](shared-hall-of-fame.md) describe the implemented Original
behaviour. This plan defines their extension.

## Timing calculation

Give every ordinary simulation step an integer timestamp separated by 120,000
microseconds. Establish the before/after stage correspondence explicitly,
including the initial observation and recovery observations that share a
presentation boundary. Host render duration, pause time and wall time discarded
by bounded catch-up add no scoring time. Each executed catch-up step still adds
one ordinary 120 ms interval.

For a valid bracket, let `a` be the forward distance remaining before the line
and `b` the forward distance beyond it. Estimate the crossing at:

```text
crossing_us = previous_us + round(interval_us * a / (a + b))
elapsed_us  = finish_crossing_us - start_crossing_us
penalty_us  = recovery_count * 260000
scored_us   = elapsed_us + penalty_us
rank_ms     = (scored_us + 500) / 1000
```

Use checked integer arithmetic, integer division and a documented nearest
rounding rule with half ties upward. Round crossing timestamps to microseconds;
round scored results to milliseconds only at the display/ranking boundary.
Retain the microsecond value for validation and race totals, without using it
to break a displayed millisecond tie.

Start at the first observed accepted line crossing and finish at the next
accepted crossing. Attaching during a lap waits for a start boundary. Use the
same physical car reference that the native track-progress calculation uses.
Enhancement observers never invent a lap crossing or change a native lap count.

Charge 260,000 microseconds once per actual recovery, independently of its two
internal timing callbacks. Track recovery identity and event order so duplicate
observations cannot double-charge it. A recovery belongs to the lap after its
start event and through its finish event; retain native stage order for events
sharing a timestamp. Recoveries before the first observed start do not charge
the first full lap. Interior recovery cuts do not by themselves invalidate a lap
whose endpoints and penalty count are reliable.

A three-lap result uses the interval from the first start to the third finish
plus all penalties in that interval. Require all three constituent laps to be
eligible and reliably timed. Sum unrounded durations and penalties, then round
the total once. An interrupted lap or partial race cannot produce a completion.

## Crossing method and confidence

Selected method: **fixed-point-world-plane-v1**. Intersect the straight movement
between full fixed-point car positions with the shared edge of the two timing
cells. All eight supplied tracks, in both leagues and both inspected asset packs,
use straight type-0 geometry on each side, with equal 2048-unit progress lengths,
no reversal and a common cardinal direction. Validate those conditions and cell
adjacency before constructing the gate. Unsupported geometry remains uncertain.

In raw world-position units, a cell is `0x800000` wide. For travel in a positive
axis direction, the gate is the lap cell's lower edge; for negative travel it is
the upper edge. Signed distance is the coordinate difference multiplied by the
forward direction. The unrounded intersection fraction is
`-previous_distance / (current_distance - previous_distance)`. Use the physical
reference at RAM `0x1bcd8/0x1bce0`, also consumed by native track progress. Read it
after progress processing, retaining its original low-word-zero adjustment.

The recovered progress calculation shifts world position right by 12 bits. This
discards spatial information before interpolation. Direct world-plane intersection
avoids that quantisation without adding a trajectory model or sampling renderer
geometry. The shared implementation lives in `src/race/crossing_timing.h`;
`tools/replay/timing_crossing_probe.h` retains the legacy progress calculation
only as a development comparison.

A reliable estimate needs an accepted native event, successful track lookups at
both observations, adjacent forward timing segments, an interval of exactly
120,000 microseconds, compatible geometry and an unambiguous recovery count.
Require signed distances to bracket zero in increasing order, with positive
displacement and the transverse intersection inside the finite cell edge.
Neither observation may be a recovery pose, and restart identities must match.
Treat an exact endpoint as fraction zero or one; zero displacement is uncertain.
Segment numbering can wrap; reverse travel, skipped segments, stale lookups and
duplicate timestamps cannot qualify. Airborne travel remains eligible when the
original accepts it and the geometric bracket is valid.

Represent confidence and fallback reasons explicitly. Missing or invalid
brackets, teleports and ambiguous geometry produce an observed-boundary estimate
marked approximate. Keep its Original eligibility unchanged. An uncertain
boundary makes each dependent Enhanced lap, and any dependent race total,
unrankable; later laps can qualify once both endpoints are reliable.

Microsecond storage and millisecond resolution describe the measurement model.
They do not promise physical accuracy within one millisecond between discrete
simulation states. Higher-order trajectory fitting and physics substepping are
outside this implementation.

## Stage 1 validation

The dedicated prototype and asset-free mathematical tests compare world-plane
intersection with progress interpolation without changing production code.
`scripts/check_native_timing.py` builds and runs both through CMake/CTest and
retains identified evidence under `analysis/timing-estimation/`. Use
`--configuration Debug` for the matching Debug proof and `--skip-build` only
with matching source/executable identities. The wrapper verifies the authored
controls and captured entry state against their recorded provenance.

The proof covers 32 track/league/asset contexts, 48,384 constructed native
brackets, 17,847 ordinary native updates and 36 accepted crossings. Constructed
positions pass native geometry and crossing eligibility checks; they are
synthetic cases rather than driven laps. The retained authored input supplies
four boundaries and three complete laps on Little Ramp. Fresh 512-update drives
exercise each track's initial line crossing and recovery in both leagues.
Other tracks do not yet have complete driven laps in this proof.

World estimates match exact rational intersections to nearest-microsecond
rounding. In the driven cases, progress interpolation differs by up to 1,152
microseconds. Deliberately slow constructed crossings reach 118,125 microseconds
of disagreement because of progress quantisation. On the retained three-lap
replay, individual boundary differences are 9–344 microseconds. These comparisons
measure quantisation and model behaviour, not physical ground truth between
simulation updates.

The timestamp audit confirms that the wheels and subsequent timing callback
observe the same position and progress. Ordinary samples retain the existing
outer index `n * 120000` epoch. Both internal recovery observations use
`(n + 1) * 120000`, also the next ordinary observation's timestamp. The two packs'
drives exercise 32 recoveries and 64 internal recovery observations. Reject these
cuts and duplicate timestamps when estimating a boundary; no extra elapsed
movement time can be inferred from the internal callbacks. Initial observation
history is empty, so an event before a second normal sample remains uncertain.
An overall epoch shift to label the initial state zero is harmless only if all
movement, boundary and presentation timestamps shift together.

The asset-free CTest covers 144 mathematical and rejection cases, including all
four directions, half-up rounding, endpoints, modular segment wrap, reverse and
stationary motion, missing/invalid brackets, recovery, off-grid data, world
bounds and the finite edge extent. Driven observer-enabled and observer-free
states match full simulation memory after every update. Original CPU execution
is outside this development proof. Current receipts, fixture/source/executable
hashes, confidence counts and raw CSV evidence are in
`analysis/timing-estimation/check-results.json` and its `primary/` and
`alternate/` directories; the method decision is retained in `decision.md`.

## Independent timing capture

`app/race/lap_timing.*` owns a small observer for every `RaceScene`, independently
of PB/Hall adapters, ghost visibility and recording buffers. The const
`state/human_timing_adapter.h` reads the native car reference, segment, progress,
off-track flags and player identity at the wheels boundary. The scene supplies
the validated movement timestamp, lookup result and native recovery identity.
Accepted `HumanLapCrossing` events supply the Original result after its native
clock slot has been cleared.

The observer tracks adjacent movement observations, start/finish confidence and
cumulative recovery counts. It charges each increase in recovery identity once,
so the two internal callbacks do not double-charge a reset. Boundary snapshots
exclude pre-start recoveries from the next lap and assign a same-timestamp reset
by native event order. A decreasing recovery identity invalidates intervals
across that discontinuity; later intervals can resynchronise.

`app/records/enhanced_time.*` owns the shared boundary and time values.
`EnhancedTime` contains the method version, elapsed/penalty/scored microseconds,
recovery count and confidence. `hasEstimate` distinguishes a usable approximate
value from invalid clock arithmetic or unknown penalty accounting. `rankable()`
requires a valid estimate and reliable confidence. Millisecond conversion rounds
half upward without overflowing at the signed 64-bit limit. Reliable race
endpoints cannot hide an uncertain intermediate lap.

Only consecutively observed full laps produce a `LapTimingCompletion`. An
attached partial lap waits for a new boundary; missing counters invalidate the
partial interval. Duplicate events produce no second result. A competitive total
requires the three full laps ending at native counter four and retains the
native cumulative time alongside the unrounded Enhanced total. Practice has no
competitive total. Quit and terminal transitions stop partial capture.

`RaceScene::takeTimingCompletions()` drains metadata from the latest successful
step; callers must drain it before the next step. This bounded handoff is
independent of ghost capture. Existing `LapCompletion` objects also carry the
optional Enhanced metadata, including time-only completions after a recording
overflow. Original PB/Hall processing and all existing codecs still use their
Original fields. Enhanced delivery consumes the independent metadata alongside
the Original completion without changing its fields or identity.

Original ghost samples retain their existing progress-aligned boundaries.
Enhanced recording persistence binds its own validated boundaries and sample
offsets. The shared movement capture now produces those recordings; it does not
reinterpret an existing Original recording as an Enhanced ghost.

Run `scripts/check_native_lap_timing.py` for the Release capture receipt, or add
`--configuration Debug` for a separate Debug receipt. Unit checks cover both
boundary fractions, rounding race totals once, recovery callbacks and placement,
confidence resynchronisation, duplicate/partial events, arithmetic bounds and
ghost-buffer overflow. Native integration covers 16 fresh track/league contexts
and twelve full laps across unrecorded Practice, recorded Practice, competitive
play and local multiplayer fixture routes. It compares final full memory/RNG
against independent native execution, retains Original snapshots, and verifies
that render queries cannot advance timing or mint completions. Identified
results live under `analysis/lap-timing/`; existing ghost/Hall and test-only
original-code regressions remain covered by `check_native_laps.py --verify-original`.

The player selects Original or Enhanced presentation while saving both eligible
categories independently. Development/original-code renderers retain their
explicit Original defaults.

## Component changes

| Owner | Planned responsibility |
| --- | --- |
| `app/race/` | Independent timing observer, crossing estimation, recovery accounting, completion metadata and category-specific PB selection |
| `src/race/` | Pure fixed-point crossing estimator and validated timing-gate geometry |
| `src/state/` | Const adapters for any additional recovered-layout observations; preserve original stage order |
| `app/records/` | Timing identity, Enhanced millisecond ordering, ties and bounded Hall boards |
| `app/persistence/` | Separate Enhanced PB/Hall formats and storage, validation, retries and recovery generations |
| `app/session/` | Deliver both categories once, bind career/track/league, drain completions and retain continuation isolation |
| `app/presentation/`, `app/launcher/`, `app/menus/` | Timing preference, timer composition, category labels, Hall filter and ghost context |
| `tests/unit/`, `tests/integration/`, `tests/oracle/` | Arithmetic, integration, storage and unchanged-original comparisons in their existing layers |
| `tools/replay/`, `scripts/` | Development comparison output and repeatable verification |

Keep timing observation active independently of ghost visibility, recording
capacity and the selected category. A completion carries the Original time and
the Enhanced elapsed time, penalty count, scored time, confidence and method
identity. Ghost samples are optional completion data. Recording overflow may
remove a ghost, but cannot invalidate a reliably measured Enhanced result.
Retain a qualifying Enhanced PB time even when its recording is unavailable;
Practice then has no PB ghost for that result.

Each category compares PBs independently. Emit Hall completions independently
of PB improvement, clearing, later race loss or quit. Use retry-stable identities
that distinguish category and timing method while preserving Original identity
behaviour. A failed write in one category cannot prevent capture or publication
in the other.

## Persistence and compatibility

Preserve existing Original archives, PB files, recovery copies and readers. Give
Enhanced Hall data a separate versioned namespace beside the current Hall root,
with its own index, object directory, lock and recovery generations. Give
Enhanced career PBs their own namespace beside the current `ghosts/` tree.
Use the same isolation for normal Windows paths and development `--data-dir`
overrides. This keeps older executables and Original garbage collection from
interpreting or removing Enhanced objects.

Keep gameplay-rules identity, track-content identity, timing category, timing
method version and file-format version distinct. Version the selected estimator
and the 260 ms penalty rule together as the Enhanced timing method. Future
changes to scoring or estimator semantics need a new compatibility identity.
Existing records and ghosts remain Original; do not fabricate Enhanced precision
from old digits or automatically promote saved movement durations.

Enhanced records retain elapsed and scored microseconds, recovery count and
confidence metadata. Enhanced ghost data also retains start/finish evidence and
ordered recovery offsets so playback can validate penalty attribution. Bind
ghosts to their category, method and immutable attempt. Recalculate encoded size,
sample/event bounds and worker budgets for the new formats; keep the existing
20-entry limit per board, with 32 boards per timing category. Timing eligibility
must not depend on a recording buffer's resource ceiling.

Reuse atomic replacement, checksums, backup generations and asynchronous storage
workers. Unsupported or corrupt data remains protected under the existing
recovery policy. Category clearing affects the selected private PB and suppresses
its replacement for the current partial lap, while the other category and Hall
capture continue. Career deletion covers both private namespaces. Preserve Hall
no-reset/no-delete behaviour and career checkpoint compatibility.

### Implemented storage foundation

`app/records/enhanced_records.*` owns Enhanced PBs and Hall models. Both compare
the rounded displayed millisecond, never hidden microsecond digits. PBs require
a strict improvement and may contain a time without a ghost. Hall boards retain
20 entries each across eight tracks, two leagues and lap/race categories. Ties
share competition rank and retain insertion order; a full board rejects a tie
with its final entry. Uncertain or unavailable estimates cannot enter either
model. An existing attempt ID cannot be retried with different record content.

`app/persistence/enhanced_record_codec.*` defines separate version-1 table, PB
and Hall-ghost formats with checksums and distinct magic values. The catalog
binds track content, gameplay rules and timing method independently. Recordings
include reliable start/finish boundaries, raw movement samples, bridge shapes
and ordered recovery offsets. Validation recomputes the score from the endpoints
and recovery count, checks sample coverage and binds each recovery to a cut.
Original files are neither imported nor reinterpreted.

| Data | Enhanced namespace |
| --- | --- |
| Private career PB | `ghosts-enhanced-v1/<career>/<track>-normal.scr-ebest` or `-super.scr-ebest` under the existing private data root |
| Shared Hall | Sibling `<original-hall-name>-enhanced-v1/`, including installation `hall-enhanced-v1/` and development `shared-hall-enhanced-v1/` |
| Shared index | `index.scr-ehall` and its own two backups, temporary file and lock |
| Shared lap recording | `ghosts/<attempt-id>.scr-eghost` inside the Enhanced Hall root |

`NativeCareerStore` exposes read/save/clear operations and a separate Enhanced
failure query. Its existing envelope uses a new payload kind; Original kinds
and checkpoints are unchanged. Reads choose the fastest valid PB among the head
and two backups, retaining the head on a tie. Writes and queued retries retain
the fastest candidate and preserve a sole fastest backup before rotating files.
Unsupported compatibility in any copy blocks replacement; corrupt copies are
preserved before repair. Clearing one private category leaves the other intact,
and career deletion removes both private categories without altering the Hall.

`EnhancedHallArchive` and `EnhancedHallStore` reuse the locked, atomic file
protocol in `hall_archive_engine.h` with the Original archive. Policies keep
their paths, index identities, codecs and garbage collection suffixes separate.
Ghosts publish before their index references; collection protects references in
all readable generations. The Enhanced worker exposes queue backpressure,
failure and retry independently. Completion IDs salt the run identity with the
Enhanced category and method before applying the unchanged completion-ID rule.
Both workers participate in live delivery, safe departure and retry handling.

Encoded limits are 66,728 bytes for a full Hall table, 3,019,268 for a PB and
3,019,196 for a Hall ghost. Recordings permit at most 65,536 movement samples,
64 bridge shapes and 65,536 recovery events. The shared worker defaults to a
64 MiB pending budget and accounts for vector allocation capacity, returning
`Full` when a candidate cannot fit. Immutable captures must meet the compact
allocation bounds; the worker encodes them away from the driving loop.

Run `scripts/check_native_enhanced_store.py`, with `--configuration Debug` for a
separate Debug receipt. It checks models/codecs and private/shared storage,
including time-only PBs, ties, failed writes, unsupported copies, isolated clears,
career deletion, backup recovery, garbage collection and concurrent writers.
The wrapper also runs Original PB/Hall regressions and both category-specific
process proofs: each merges 20 attempts from two writers, then terminates owned
children at seven transaction boundaries and verifies deduplicated retries and
usable ghost references. Receipts and disposable data live under
`analysis/enhanced-records/`. These checks use synthetic records and do not access
real player data; process interruption does not establish power-loss or real
second-account behaviour.

## Session delivery and ghost targets

`LapRecorder` shares raw movement capture between the two categories. At a
reliable Enhanced completion it copies the raw poses and rebases their offsets
to the full-position crossing, preserving the Original progress-aligned record.
It retains neighbouring samples, moving bridge shapes and recovery cuts. Invalid
or overflowing movement data removes only the Enhanced ghost; a reliable time
can still improve an Enhanced PB and enter the Hall. No Original saved recording
or old duration is promoted to Enhanced.

The recorder compares private PBs independently, and `HallRaceRecording` delivers
every eligible completion to both Hall workers regardless of PB improvement.
Enhanced laps require reliable timing; competitive totals require the observer's
three consecutive full laps. The actual native human supplies the roster name,
track and league. Multiplayer contributes Hall records without acquiring a
private PB owner. Session delivery drains both queues after each native step,
before terminal checkpoint handling. Reloading a checkpoint or saved target
creates no new attempt.

`native_enhanced_session.cpp` restores the matching career/track/league PB at race
entry and resubmits retained unsaved improvements. Original and Enhanced failures
remain independent; all workers participate in Saved status, Retry, close and
career departure. Both private categories are forgotten when a career is deleted.
Live Hall delivery uses `enhancedCapturePendingMaximumBytes`, recalculated for
every possible retained top-20 capture; allocation backpressure cannot silently
remove a valid live recording.
Clear removes only the effective private category and its playback target,
suppresses replacement for the current partial lap, and leaves Hall capture and
the other category active. The next full candidate can qualify normally.

`TimingCategory` and `NativeGameSession::setTimingCategory` bind the effective
preference. Changing it selects retained playback epochs without
restarting either capture. Each category selects its next PB target at a crossing;
the current attempt retains its target. Playback uses raw elapsed movement time,
with the existing interpolation, recovery cuts, fade and Draw Bridge guard.
The 260 ms scoring penalty never inserts poses or extends a ghost's movement.
Repeated and reverse queries cannot update timing, visibility or records.

`PinnedEnhancedHallGhost` binds a rival's method, category, attempt and immutable
movement. `EnhancedHallStore::requestTarget` resolves the current retained row and
validates/loads its recording on the worker. Session requests can be cancelled;
stale responses are ignored and Back takes priority over a completed resolution.
A challenge temporarily overrides the selected category, preserving the rival
through private PB improvements, clears and visibility changes. Preview cancel,
race exit and close preserve the career continuation, suspended season and RNG;
the global category applies again on return. The Hall's explicit timing filter
uses this path for Enhanced Ready rows.

Run `scripts/check_native_enhanced_sessions.py`, or add `--configuration Debug`.
Receipts under `analysis/enhanced-sessions/` identify sources, fixtures and
executables. Asset-free constructed checks cover independent PB winners, boundary
rebasing, ties, recovery offsets, time-only overflow, category clearing, pinned
targets and bridge suppression. Native checks cover all eight tracks/both leagues,
nine complete laps across Practice/competitive/multiplayer fixture routes, two
competitive totals, independent failure/retry of all four PB/Hall destinations,
reload/time-only targets, asynchronous resolution/cancel and challenge continuation.
They verify visible Enhanced ghosts in all four views and compare full native
memory/RNG with capture disabled. The retained authored route supplies complete
Little Ramp laps; other tracks use fresh partial drives, not complete-lap claims.
`scripts/check_native_laps.py --verify-original` supplies the Original player,
recording and test-only original-code regression checks.

## Settings and presentation

Display settings version 10 adds `timing=original|enhanced`. New installations
and versions 1–9 migrate to Enhanced while retaining their other choices.
Explicit Original choices round-trip. Launcher and F10 Settings share the field;
preview, failed Apply/retry and Cancel use the existing transaction. Restore
original defaults explicitly selects Original timing and navigation.

The effective timing category selects the running timer, completed-lap hold,
three-lap result display, PB context and default Hall board. The Hall has an
explicit Original / Enhanced filter, with category-specific rows and rivals.
Original competition reports retain their native times, record notices, winner and points;
an Enhanced duration does not imply a different race outcome.

`presentation/enhanced_clock.*` retains a value snapshot and at most sixteen
ordered state changes per native frame. A crossing key uses its estimated
movement timestamp; a recovery key uses the recovery observation's timestamp,
which can lie at the next frame boundary. Queries combine retained neighbouring
snapshots and apply only changes reached by the presented car. Future recovery
penalties and completed holds cannot leak into an earlier interpolated image.
Snapshots retain starts, cumulative recovery identities, completed holds,
record values and first-finish freezing. Native hold release follows observed
scheduler events; running values use elapsed movement plus explicit penalties.
Pause preserves the fractional presentation position. Repeated/reverse queries,
mode changes and extra refreshes cannot advance measurement, mint attempts or
alter later history.

Enhanced composition changes only the two existing clock slots, x234–283 at
y178–185 and y188–195, preserving their one-pixel cut corners. The retained native
font/palette uses six-column digits, narrow punctuation and one-pixel gaps for
normal `m:ss.mmm` times. Long minute values, unavailable fields and the `~`
approximation marker use narrower digits within the same slots. Saved finishes
use the same layout. `--:--.---` means unavailable. The lower
field shows the Enhanced lap record, or the observed three-lap result after a
competitive finish. Entry records come from compatible Enhanced PB/Hall values;
Original digits never supply invented precision. All four views retain the same
car-art scale and the selectable Original instrument path.

Hall navigation keeps ten rows per page. Track occupies the first selector row;
league and timing share the next, followed by lap/race type. Keyboard shortcuts
1/2/3 retain track/league/type and 4 selects timing. Each category resolves rows,
rank, ghost availability and challenges on its own worker. Changing filters clears
stale rows and cancels previous requests. Request/category checks prevent a late
response from crossing namespaces. The opening default follows the global
preference; an explicitly selected filter remains independent of it.

Pending-finish checkpoints keep their Original bitmap and may add two immutable
Enhanced display values, including availability/confidence. Only checkpoints
carrying that optional data use version 3; ordinary checkpoints remain version 2.
The reader retains versions 1–2, and unknown methods remain protected. Restored
finishes can switch categories without rerunning timing/scoring or changing the
saved artwork. Older finishes show unavailable Enhanced fields rather than
promoting their Original bitmap. Multiplayer saves and record formats are unchanged.

Ghost movement stays on elapsed movement time; scoring penalties never insert
extra poses or slow playback. Select ordinary Practice PB ghosts from the
effective category and apply target changes at lap boundaries. Timing-mode
changes preserve both current captures and retained presentation history. A Hall
challenge pins its rival and timing category for the challenge; the global
preference applies again on return. Hiding a ghost leaves observation and timing
active.

`scripts/check_native_enhanced_ui.py` produces identified Release/Debug receipts
under `analysis/enhanced-ui/`. It covers all nine preference migrations, explicit
Original persistence, Apply/Cancel/failure, exact presentation-boundary keys,
future recovery rejection, approximate/unavailable values, all four apertures,
three authored interpolated crossings, Hall filtering/ties/race formatting,
pinned categories and saved-finish compatibility. Existing session, autosave,
pause/focus/controller and Original timer checks run alongside it. PNG previews
are retained; `approximate.png` is an explicitly constructed presentation case.
The live launcher/F10 keyboard check uses an isolated disposable data root.

## Implementation stages

1. **Validate crossing geometry — complete.** Audit observation timestamps and the native
   boundary/reference definition. Compare progress and world-position methods
   with known mathematical cases and identified replays. Retain the method
   decision, validity conditions and comparison evidence under `analysis/`.
   Exit: one documented deterministic method and explicit fallback reasons.
2. **Build timing independently of recording — complete.** Add integer time values, boundary
   confidence, recovery accounting, lap/race accumulation and const adapters.
   Integrate dual completion metadata without changing Original processing.
   Exit: reliable results survive ghost overflow and full simulation/RNG
   comparisons remain identical.
3. **Add records and persistence — complete.** Implement category-specific ordering, PBs,
   separate Enhanced namespaces/codecs, retry identities and storage workers.
   Exit: tie semantics, Original compatibility, time-only results, interrupted
   writes and concurrent access pass isolated tests.
4. **Integrate sessions and ghosts — complete.** Capture both categories through Practice,
   career and local multiplayer routes; restore matching PB targets and pin Hall
   category/identity. Preserve clear, quit, save failure and career continuation.
   Exit: each completion reaches each eligible category once, and ghost movement
   remains independent of penalties and record selection.
5. **Add settings and presentation — complete.** Implement preference migration, category
   filtering, millisecond formatting and timer snapshots. Inspect the HUD and
   Hall in all views, with smoothing on/off, pause, recovery and mode changes.
   Exit: displayed crossings match presented movement and UI queries preserve
   simulation, saved records and future history.
6. **Verify and stage — complete.** Run the affected Release/Debug checks and fresh original
   comparisons, update component/player documentation, build and test with CMake
   presets, then stage through CMake Install and verify the release contents.
   Exit: current source/fixture/executable receipts support the declared scope.

## Verification

| Area | Required cases and existing verification route |
| --- | --- |
| Timing arithmetic | Exact endpoints, fractions, positive duration, rollover, overflow, half-millisecond ties and race totals rounded once; unit CTest |
| Crossings | Both endpoints, forward/wrap/reverse/high-speed/airborne cases, lookup failure, missing brackets, recovery cuts and penalty ordering; lap integration tests |
| Original isolation | All eight tracks and both leagues, identical native memory/RNG/outcomes with either timing mode; `scripts/check_native_laps.py --verify-original` |
| Record models/codecs | Separate rankings and PBs, ties/cutoff ties, duplicate attempts, time-only results, invalid metadata and legacy reads; `scripts/check_native_enhanced_store.py` |
| Durable storage | Both namespaces, retry/failure isolation, backups, unsupported files, process interruption, concurrent writers and garbage collection; `scripts/check_native_enhanced_store.py` |
| Session/UI | Practice, career, multiplayer, pinned rivals, clear, quit, interrupted races and settings migration/Cancel; `scripts/check_native_game.py --verify-original` |
| Presentation | Frame-rate independence, executed catch-up, discarded stall time, pause/focus loss, smoothing, repeated/reverse queries and all four views; `scripts/check_native_rendering.py` and `scripts/check_native_render_audit.py` |
| Ghosts | Raw movement timeline, independent category targets, recording overflow and Draw Bridge suppression; lap checks plus `scripts/check_native_ghost.py` and `scripts/check_native_ghost_bridge.py` |

Extend the existing wrappers and CTest registrations for new cases rather than
claiming existing passes cover the enhancement. Use `windows-release` and
`windows-debug` for native checks, and `windows-oracle` for test-only original
execution. Production continues to exclude oracle headers and Musashi linkage.
Inspect interactive keyboard/controller flows and rendered layouts manually.
Retain comparison scope and source/fixture/executable identities with evidence
under `analysis/`, following [development](../development.md).

### Release evidence

`analysis/enhanced-release/check-results.json` identifies the checked sources,
fixtures, executables and staged player. Its scope combines the complete native
Release suite, affected Debug timing/storage/session/UI checks, separate-process
storage interruption proofs, fresh Original-code comparisons and inspected UI
previews. Full-scene ghost rendering proofs run in Release. Interrupted optional
runs are recorded separately and do not count as passes.

The staged-player smoke check uses isolated data and keyboard input. Controller
events have native integration coverage; physical controller hardware, actual
power loss and a real second Windows account remain outside this evidence.
Release contents and the staged/build executable hashes must agree before a
release receipt can pass. Historical receipts still require identity checks
before reuse.

Completion requires preserved Original data and gameplay, independent Enhanced
rankings with conservative eligibility, deterministic millisecond results, and
matching timer/ghost presentation. All six stages are implemented and the checked
Release player is staged through CMake Install. The release receipt declares the
completed checks and their limits; it does not establish exhaustive equivalence.

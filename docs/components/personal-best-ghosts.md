# Personal-best lap ghosts

Status: agreed player-facing design; the static-track and Draw Bridge development
proofs are implemented, including the painted-boundary depth fix. Lap recording,
storage, player controls and broader production acceptance remain to be built.

## Player contract

- Retain one personal best, including its ghost, per career, circuit and
  Normal/Super league. Both Practice and career league races feed the same
  target: the faster eligible human lap wins regardless of its source mode.
  Use career identity, not driver name. Original and Modern controls share the
  target; retain source mode and control preset as recording metadata. Local
  multiplayer recording is deferred: its roster slots do not own career targets.
- Use the original game's lap-start, completion and acceptance rules. Falls,
  recovery, damage and the fuel conditions actually encountered do not add new
  eligibility restrictions. Compare original completed-lap times; ties retain
  the existing best. Record the human player's car, not an AI opponent. Opponent
  contact is part of a league lap, not an added reason to reject it.
- Record improved laps automatically in both modes, including while ghost
  display is off. An incomplete attempt cannot replace a best; a later race loss
  or quit does not discard an already accepted completed improvement. Observe
  completion while racing rather than waiting for a successful race exit.
- Playback remains Practice-only in the first version. Select its target at
  each lap boundary and keep it fixed throughout that attempt. An improvement
  becomes the target on the following lap. Fade after the ghost's recorded
  finish; restart at the player's next lap boundary, without looping or waiting
  at the line. A faster league lap becomes the target for the next Practice lap.
- Pause freezes playback. Changing visibility does not reset playback: enabling
  it midway through a lap reveals the ghost at the corresponding elapsed time.
  A ghost has no collision, audio, gameplay effects or bridge occupancy.
- Draw Bridge bests may come from different bridge conditions. Preserve the
  recorded trajectory and live bridge behaviour; temporarily suppress the ghost
  around an incompatible crossing, as described below.

Ghost bests are independent of Hall of Fame eligibility. A faster Practice lap
can replace the saved ghost without adding a Hall entry. League laps continue
through the existing competitive Hall rules independently of ghost replacement;
recording must not invoke scoring, progression or legacy save encoding.

## Settings and feedback

Ghost display defaults to on in Practice, with a persistent toggle in the
existing host settings. The Practice pause menu shows the target time and a
confirmed action to clear that career's best for the current circuit and league.
Clear removes both the best time and its recording, regardless of which mode
produced them. During a lap it also removes the active ghost, cancels that lap's
recording candidate and starts a fresh candidate at the next normal lap boundary.
This explicit reset is the exception to keeping an attempt's target fixed.

Show the same brief new-personal-best notice after an improvement in Practice or
a career league race. League races record laps without displaying a ghost. Keep
feedback in host presentation rather than changing original cockpit artwork.
A continuously displayed ahead/behind delta, recording browser and manual target
selection are deferred. Recording remains automatic independently of display
visibility.

## Recording and playback

Use a compact movement recording rather than a second simulation or a dump of
`RaceFrame`. Capture position, orientation, lap-boundary information, recovery
cuts and the additional body/wheel information established by the rendering
proof. Retain neighbouring samples around a crossing so playback endpoints can
be aligned to the game's crossing information.

Movement timestamps follow the existing gameplay/presentation timeline, not
wall-clock time or displayed lap-clock digits. Keep the original completed-lap
time separately for ranking. Crossing corrections and the internal updates in
recovery sequences must be covered by the timing proof; sample count alone does
not establish the lap time. Cut at recovery discontinuities instead of blending
across the circuit. Evaluate smoothing against the same displayed timeline as
the player's view, including its existing one-update latency.

Recordings need format, track-content and gameplay-rules identities, league,
career ownership, completed time, source mode and control-preset metadata. Source
mode is provenance, not a separate target key. A presentation-only change should
not invalidate a recording solely because the executable changed.
Derive explicit duration and sample bounds from the game's timing and recovery
behaviour before fixing the codec. Full simulation memory, original assets and
the development control CSVs are not the player ghost format.

Store best time and recording as one durable replacement, independently of
season checkpoints and the Hall of Fame. Reuse the storage guarantees in
[sessions](sessions.md): bounded validation, immutable write jobs, recovery and
atomic replacement. Failed writes retain the latest in-memory best and the last
good file; do not report a completed save before the worker completes. Clearing
must drain or invalidate pending writes so an old job cannot restore the target.
Preserve unreadable or unsupported files rather than silently overwriting them.

## Rendering proposal and proof gate

Render a small world-space polygon car in the original opponent's visual style.
Verify the model's dimensions, body origin, wheel placement and pitch/roll
conventions against existing presentation data; the recorded camera/car position
must not simply be assumed to be the centre of a complete exterior model.

The original opponent builder is a visual reference, not a ready-made ghost
renderer: it uses projected wheel footprints, height clamping and a single
opponent painter marker. It does not define a unique rigid 3D exterior. The ghost
retains recorded jumps and orientation without borrowing live opponent state or
the recovered line budget.

Start with a cool tint, approximately 30–40 percent opacity, and additional
fading near or overlapping the player's car. Tune these values during the proof.
Resolve the ghost's visible surfaces into its own layer before blending, so
overlapping translucent faces do not accumulate opacity. Omit shadows, effects
and sound in the first version.

Keep the existing ordered world renderer. The proposed composition is world,
visible ghost pixels, then existing foreground effects/cockpit/HUD according to
their presentation ownership. Use the same angular projection, current camera,
clipping and view aperture as the track. Ghost rendering uses independent bounded
storage and cannot change original scene commands or allocation limits.

Capture depth alongside the original ordered road/side fills when a ghost query
requests it. The earlier auxiliary mesh mask left gaps because its projected
triangles did not cover the same clipped boundaries as the recovered painter.
Projected endpoints now carry reciprocal forward depth through clipping and
viewport closure; the existing scanline fill interpolates depth over exactly the
pixels that receive colour. Later painter commands replace both colour and depth,
including when that differs from a generic nearest-surface sort.
Rejected-line endpoints used for viewport closure retain attributes within the
source segment. A missing depth attribute remains unknown; no constant or nearby
edge fallback can make the complete-coverage test pass.

Depth metadata is private presentation state and is not serialized by recovered
RAM adapters. Ordinary queries retain their colour path; the ghost consumes a
copied depth layer and its own resolved car layer. No extra road geometry,
original line allocation or simulation writes are required. A standalone mesh
depth path remains available only for renderer diagnostics.

### Static-track proof

Run `scripts/check_native_ghost.py` after loading the development environment.
It builds the optional `tools/replay/native_ghost_proof.cpp` entry point, runs
focused CTest checks and retains source/fixture/executable identities with results
and visual previews under `analysis/ghost-proof/`. See the
[tool instructions](../../tools/README.md#static-ghost-rendering-proof).

The proof derives a trajectory from the retained authored lap and exact captured
entry state, checking native updates against the identified retained original
trace. It executes no original CPU code. The same path is sampled twelve updates
ahead of the camera for an illustrative target, not as a distinct faster lap.
These development fixtures introduce no capture requirement for the player.

The exterior is native artwork calibrated against the recovered opponent. A
synthetic flat-support observation of `setupOpponentTrackGeometry` on private
typed state yields a reference footprint 5.0625 world units wide with rows 7.5625
units apart for the default fixture. These are visual calibration measurements,
not a recovered player collision volume or four physical wheel centres. The
body uses a sloping counterpart of the recovered procedural vertices 4..11,
within the middle half of the footprint; tyre ends fit within its rows. The
model contact offset follows flat recorded player driving. It does not infer an
extra cabin or nose from player suspension lever arms.

`opponent-footprint.json`, `proportions/measurements.csv` and `proportions.png`
retain the observation and opaque comparisons against `buildOpponentCarFrame` at
matched support points, four distances and rear/30-degree/45-degree views. Width
and rear height have bounded pixel checks. The original procedure changes shape
with view, so a rigid exterior cannot claim exact silhouette identity at every
angle; oblique body/height and suspension articulation remain approximations.
Grounded and airborne previews accompany comparisons with no ghost, an unmasked
ghost and the track-masked ghost.

The proof sweeps every retained update and quarter-update presentation position
in all four apertures, requiring zero painted road/side pixels without depth.
The report separately counts gaps under the ghost and records the static boundary
gate. Depth follows interpolated original painter spans; it is not a claim of
exact physical visibility for all scenes or acceptance of Draw Bridge behaviour.

Checks include resolved single blending, clipping, partial/full road and sidewall
occlusion, ordered depth replacement, clipped-boundary depth coverage, overlap
fade, negative-height interpretation, all four views, zero opacity baseline
equality and repeated/reverse queries. Independently evolved
live scenes compare all 512 KiB of exit memory after further updates, including
RNG. Retained pose/command checks have their declared projection; they do not
claim exhaustive identity of every presentation field. Frame time is measured
for the sampled queries, not promised for all scenes or machines.

## Draw Bridge

The [simulation contract](simulation.md) fixes the update order: the driving
frame captures track geometry before the bridge update. Car occupancy holds
bridge geometry, so matching phase at lap start cannot ensure matching geometry
throughout two differently driven laps.

The live bridge responds only to its existing simulation inputs. Ghost position,
visibility and recorded bridge state never affect it. Do not alter playback
speed, attach the ghost to the live ramp or restore recorded geometry into the
live circuit.

Retain compact bridge-shape snapshots with the movement recording, captured at
the same presentation boundary as its car pose. Compare the actual local ramp
geometry with the live frame's retained bridge snapshot, rather than comparing
phase numbers alone. Distinct phases can share a shape; held geometry and update
history also matter. Compression, matching tolerance and sample representation
remain proof decisions.

| Ghost position and conditions | Presentation |
| --- | --- |
| Away from the affected bridge area | Display normally |
| Affected crossing, compatible geometry | Display normally |
| Affected crossing, incompatible geometry | Fade out locally |
| Clear of the affected approach/crossing/landing | Fade back in |

Playback continues at its recorded time while hidden. Use short fades and a
stable compatibility decision to avoid flicker; repeated render queries must not
advance fade or playback state. A small bridge-conditions-differ indicator
explains suppression. Determine the affected area and transition behaviour from
crossing/landing evidence, not a whole-lap phase mismatch.

### Draw Bridge proof

Run `scripts/check_native_ghost_bridge.py` with the native asset pack available.
The optional `tools/replay/native_ghost_bridge_proof.cpp` entry point constructs
a native league approach, records 512 updates, then exercises matching league
conditions, different Practice conditions and a different phase with equal
crossing geometry. The real opponent starts on the bridge and holds its shape
through the normal AI/occupancy path. Starting positions and initial phase are
synthetic fixture choices; there is no midrace state injection, captured input,
original CPU execution or claim of an earned completed lap.

At the same pre-bridge-update presentation boundary as the pose, the proof
retains the 72 mutable raw geometry bytes and the moving cached sections' flags,
segment identifiers and heights. It compares these values exactly, without using
phase or AI guidance as a compatibility key. Both the dense-near and cached-far
geometry matter. Strict equality is a conservative proof choice; production
matching tolerance and the durable recording format remain separate decisions.

The source trajectory enters its guard on approach segments 49–50 and crosses
moving segments 51–55. After crossing, it remains guarded until two consecutive
source updates report native suspension contact beyond those segments, with no
off-road-side flag. A recovery cut clears the previous region. Mesh clearance
is retained as a diagnostic, not used as a substitute for dense-surface contact.
An incompatible observation latches suppression until the guard clears, avoiding
flashes when shapes briefly match during the crossing or landing. Opacity changes
over 240 ms of the existing presentation timeline. Repeated, paused and reverse
queries evaluate retained transitions without advancing them. Recorded movement
continues unchanged while hidden.

All four apertures at exact and half updates exercise painted-depth coverage,
zero-opacity world equality and ordinary rendering after ghost queries. Paired
scenes finish with identical full 512 KiB simulation memory, including bridge
state and RNG. Reverse and repeated queries against retained snapshots reproduce
the live images. The separate bridge-driving integration and original-code bridge
oracle cover their existing declared contracts; this proof's independent native
trace is a native consistency check, not an original-code comparison.

Identified results, fixture seeds/controls/traces, three previews and
`comparison.png` are written under `analysis/ghost-bridge-proof/`. A private
observer camera follows behind and above the recorded trajectory in each live
world; it never moves either car. Preview captions include a development
bridge-conditions indicator. These camera/caption choices are not player UI.
See [tool instructions](../../tools/README.md#draw-bridge-ghost-proof).

## Implementation sequence and acceptance

1. Prove car placement and occlusion using a recorded trajectory on a static
   circuit. Cover grounded motion, jumps, pitch/roll, crests, sidewalls, recovery,
   camera overlap and near clipping. Select the rendering approach only after
   reviewing its relationship to the actual painted track.
2. Prove Draw Bridge matching, local suppression and reappearance. Include
   different phases with equal geometry, different shapes, occupancy holds,
   release, landing, retained-frame queries and pause. Include a league-sourced
   ghost whose recorded bridge conditions were affected by the real opponent.
   Use the live frame's geometry, never its later simulation state.
3. Integrate recording at Practice and league lap boundaries, Practice target
   selection and original completed-time ranking. Exercise first timed laps,
   crossing corrections, falls/recovery, opponent contact, loss, quit, ties,
   improvement and visibility changes. Prove replacement in both directions:
   league beats Practice and Practice beats league, without changing Hall rules.
   Confirm local multiplayer cannot replace any career's ghost.
4. Add durable storage, settings, feedback and clear behaviour. Exercise write
   failure, recovery, unsupported formats, career isolation, shared control
   presets and pending-write deletion.

At each rendering gate, inspect Original/Extended 4:3, 16:9 and 32:9 with
smoothing on and off. With identical controls, ghost on/off must leave simulation,
RNG, race outcomes and bridge evolution identical. Ghost-disabled rendering must
retain the baseline images. Repeated, reverse and alternate-view queries must
leave simulation, retained frames and future presentation history unchanged.

Use [development verification](../development.md#choose-verification-by-change)
for rendering/view isolation, recorded playback/input clocks, game flow/storage
and affected opponent paths. Production must not include test headers or link
oracle code. Keep proof fixtures and results under ignored `analysis/`, with
source, fixture and executable identities. A retained pass applies only to those
identities and the declared scope, not the whole planned feature.

Shared presentation belongs in `app/presentation/` and `src/render/`, race
orchestration in `app/race/`, persistence in `app/persistence/`, and recovered
observations in `src/state/` where needed. Development entry points belong in
`tools/replay/`; tests follow the existing `tests/` layout.

## Future online records and ghosts

The future goal is an online Hall of Fame and online ghosts shared across
players. This is distinct from the existing Hall shared across local careers.
Keep recordings self-contained with compatibility metadata, and keep target
selection separate from playback so an online source can be added later.

The first version has no upload/download, accounts, online leaderboard, public
record verification or networking. Local checksums establish file integrity,
not authenticity or eligibility for a future online ranking. Online identity,
verification and sharing policy require their own design. Computer Link remains
deferred.

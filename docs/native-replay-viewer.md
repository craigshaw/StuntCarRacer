# Native replay viewer

The viewer presents the retained authored Practice race using the recovered
native gameplay code, original cockpit assets and a native software renderer.
It loads the original game bytes locally; tracked source contains no copyrighted
image or track data. The viewer executable does not link Musashi or execute a
68000 emulator.

Double-click `Watch Native Replay.cmd`, or run `scripts/start-native-replay.ps1`,
to verify the local inputs, build and open the viewer. For a paused start use
`scripts/start-native-replay.ps1 -Paused -StartTick 272`. It uses the
same controls as the 1,463-tick original/native regression. The full track cycle
crossings are ticks 24, 505, 961 and 1,415; the race-end gate occurs at tick 1,462.
These are authored inputs chosen by a native feedback experiment, not a human
recording.

For live keyboard/controller driving, use `Drive Native.cmd` or
`scripts/start-native-replay.ps1 -Live`. This shares the same native simulation
step and renderer. See [native driving](native-driving.md) for its controls,
fixed-step clock and input recordings.

Playback controls:

| Input | Action |
| --- | --- |
| Space | Play or pause |
| Left / Right | Pause and step one replay tick |
| Home / End | Jump to the start or end |
| Timeline drag | Scrub the replay |
| W | Toggle the wireframe overlay |
| I | Toggle smooth interpolation / exact verified ticks |
| Playback buttons | Play/pause, replay, step and playback speed |

Playback defaults to **60 FPS interpolation**, with verified physics states
spaced **0.12 seconds apart** (about 8.33 updates per second) at 1× speed. The
camera position, shortest wrapped angle paths, original camera baseline/roll,
suspension placements and speedometer blend between adjacent states. The scene
is redrawn for each intermediate pose. Near geometry and visibility are rebuilt
from private copies of saved renderer state; these queries never advance physics
or write back to the verified timeline. Each exact integer tick uses its original
camera and draw commands unchanged.

Damage, wheel/flame image selection, boost and lap/race events remain attached
to verified ticks. Restart and terminal transitions hold their previous view
until the next tick, so the car does not blend across a relocation. Pausing can
retain an intermediate view; arrow-key stepping lands on exact ticks. Scrubbing
is deterministic in either direction. Press I to compare against exact-tick
playback, or launch with `scripts/start-native-replay.ps1 -ExactTicks`.

The upper-right FPS counter measures completed window presentations over the
last second and shows a paused indicator when playback stops. A high-resolution
waitable timer schedules 60 Hz deadlines independently of the replay clock;
late presentations are skipped without adding physics updates. Display load and
hardware can lower the achieved rate. This presentation does not reproduce the
original interrupt or display timing.

## Verification and exported frames

From the project root:

```powershell
. .\scripts\env.ps1
& $AmigaPython .\scripts\check_native_rendering.py
```

The wrapper checks the ADF, payload, capture, authored input and retained original
trace hashes. It checks that the gameplay source files still match the passing
reference report. Presentation changes, including renderer sources and CMake
target additions, do not invalidate an otherwise current gameplay reference.
If that evidence is absent or stale, it regenerates the original/native replay.
`--refresh-reference` forces regeneration; `--skip-build` requires a retained
build receipt matching every native source, capture adapter and executable hash.

The wrapper builds `native_replay_viewer` and `replay_interpolation_tests` with `SCR_BUILD_ORACLE=OFF`, runs the viewer
without opening a window, renders every replay tick, compares every declared
gameplay trace column with the original trace, and requires at least one complete
observed lap. It also renders the entire 60 Hz presentation timeline: **10,528
samples** for the retained race, followed by reverse-order seeks and checks that
all **1,463 exact tick images** retain identical pixels. The 14 interpolation
regressions cover wrapped positions/angles, exact endpoints, suspension placement,
discrete events and transition cuts. Original gameplay trace comparison remains
separate from these presentation checks. It exports
ticks 0, 24, 128, 272, 505, 961, 1,415 and 1,462 when present in the chosen input.
Five fractional-tick views are exported for interpolation QA.

Outputs remain local under `analysis/native-render/`:

- `check-results.json`: source, executable, input, capture, trace and asset
  identities, comparison results and lap certificates.
- `viewer.json` and `viewer-native.csv`: the executable's own comparison report
  and native gameplay trace.
- `frames/frame-NNNN.bmp` and `.png`: exported original-resolution frames.
- `contact-sheet.png`: a QA sheet assembled from the exported frames.
- `interpolated-contact-sheet.png` and `frames/interpolated-NNN.FFF.png`:
  intermediate camera views, including three poses within the same physics tick.
- `assets/assets-manifest.json`: original asset ranges, hashes, palette and
  initialized font provenance.
- `render-recovery-checks.json`: retained mesh, projection, sidewall, mountain, near-track and asset oracle
  results with source and executable hashes.

Pillow from the bundled workspace runtime converts the BMP files for visual QA.
The game renderer itself has no Pillow or Python dependency.

Fractional render positions can also be exported directly with the viewer's
`--interpolated-snapshot 86.5 --frames-dir <directory>` arguments. Headless checks
always exercise both exact ticks and the full interpolated timeline; the GUI's
`--no-interpolation` option controls playback presentation only.

## Original assets and rendering boundaries

The main cockpit image comes from the original RLE stream at runtime `0x201a4`,
which consumes 14,956 bytes from payload offset `0x11aa4`. Its palette begins at
`0x20184`. The original palette stores three significant bits per channel;
the native conversion follows the original doubling and low-bit adjustment.
The game viewport occupies x=32, y=16, width=256, height=128 within the 320×200
frame.

Original masked objects supply the top corners, wheels, engine cover, exhausts
and boost flames. Per-frame suspension coordinates and damage pixels come from
the independently evolved native state. Only compact presentation snapshots are
retained for playback; rendering does not update live gameplay RAM.

The road mesh is recovered from the original 14-byte cross-section stream at
`0x7abda`, generated by `0x65bec`. Its captured 51 cross sections produce 102
vertices and 51 quads. The mesh oracle passes 103 comparisons, including the
complete original-generated cache and all transformed vertices. The angular
projection, camera setup and screen roll follow the original routines at
`0x6249c`, `0x624f0`, `0x62518` and `0x65ec4`; 18,884 original/native comparisons
pass with zero mismatches. Local evidence is in `analysis/track-mesh/` and
`analysis/render-projection/`.

The original image decoder, palette conversion and masked-object renderer were
executed in the test-only original-code oracle: 238 comparisons, zero mismatches.
The masked checks include all six wheel images across their supported suspension
heights. To build the original-code rendering comparisons:

```powershell
cmake -S . -B analysis/native-render/oracle-build `
  -G "Visual Studio 18 2026" -A x64 -DSCR_BUILD_ORACLE=ON `
  "-DMUSASHI_DIR=$PWD/analysis/reference/musashi"
cmake --build analysis/native-render/oracle-build --config Release `
  --target track_mesh_oracle render_projection_oracle planar_image_oracle `
    track_sides_oracle mountain_horizon_oracle near_track_geometry_oracle `
    near_track_visibility_oracle software_renderer_tests
```

The original-code executables accept `payload.bin chipram.bin`; both near-track
oracles also accept the authored controls CSV for the full replay. The native
`software_renderer_tests` accepts only `payload.bin`. Use the local payload and
Chip RAM paths recorded in `check-results.json`. To retain an asset comparison
report using the built oracle:

```powershell
& $AmigaPython .\scripts\prepare_replay_render_assets.py `
  --oracle .\analysis\native-render\oracle-build\Release\planar_image_oracle.exe `
  --output .\analysis\native-render\recovery-assets
```

The original wheel rotation accumulator advances from vertical blank interrupts.
The headless gameplay replay excludes those interrupts, so the viewer preserves
the admitted wheel-frame state while showing the recovered suspension motion.
The viewer also draws the original raised-road side panels down to world Y16,
and the recovered mountain silhouettes. Side fills follow segment parity and
the captured original palette choices. Road fills follow segment parity and
the black-surface flag; strip metadata belongs to its ending cross-section.
There are 102 side quads in the retained track. The side oracle passes 9,499
original executions, including full edge construction and material ownership
for every strip, with zero mismatches.

Mountains come from five original templates, 25 variants and 32 placements.
The loader, yaw selection, coordinate substitution, roll and polygon commands
pass 6,817 comparisons with zero mismatches. Concave silhouettes use their
recovered boundary; a native coverage regression checks that the notch remains
open and that winding does not change coverage. See
[the sidewall and mountain evidence guide](native-sides-and-mountains.md).

The three closest segments use the recovered dense geometry, forced lower
vertices, visibility queue and ordered road/side fills. Yellow and brown
longitudinal highlights, conditional white cross edges and viewport boundary
closure follow the original command selection. Coarse far faces are suppressed
for all three visited segments, including ones culled by near visibility. See
[the near-track evidence guide](native-near-track.md).

Far-road subdivision, triangles and depth buffering, plus native polygon and
line raster coverage, remain presentation choices. Playback is silent.
Matching gameplay traces and draw commands does not establish pixel equality with the
original blitter, DMA, interrupt schedule or audio output.

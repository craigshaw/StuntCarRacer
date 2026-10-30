# Native architecture

The game is ordinary compiled C++17 inside a Windows event loop, with native
input, audio and software rendering. Original 68000 instructions run only in
separate verification executables. The disk-derived pack supplies data to native
decoders; retaining original code bytes in the pack does not make them executable.

## Runtime ownership

| Layer | Main sources | Responsibility |
| --- | --- | --- |
| `scr_simulation` | `src/physics/`, `src/race/`, `src/track/`, sample requests in `src/audio/` | Fixed-point physics, controls, collision, AI, race timing, restart and bridge state |
| `scr_audio` | `src/audio/engine_audio.*`, `paula_mixer.*` | Recovered engine interrupt, sample handlers and four-channel mixing |
| `scr_render` | `src/render/` | Geometry, projection, planar decoding and software rasterization |
| `scr_state` | `src/state/` shared headers | Recovered memory-layout adapters, native race stages and replay I/O |
| `scr_runtime` | `app/assets/`, `menus/`, `presentation/`, shared rule adapters | Asset/race initialization, original menus, text, records and competition rules |
| `scr_game` | `app/session/`, `race/`, `persistence/`, `input/`, `audio/` | Session/race handoff, career continuation/storage, gameplay orchestration, snapshots and recordings |
| `scr_host_ui` | `app/launcher/`, Windows launcher and display in `app/platform/windows/` | Pre-game setup, display/control UI, scaling, fullscreen and preferences |
| Windows host | `app/main.cpp`, `app/platform/windows/` | Event/presentation loop, keyboard, XInput, device playback and window lifecycle |

`CMakeLists.txt` owns production library linkage; `cmake/DevTools.cmake` defines
optional replay tools, and `cmake/NativeTests.cmake` and `cmake/OracleTests.cmake`
define separate test targets. Presets share the native build across suites, keep
oracle output separate and stage the player and licence notices in `dist/`. Production state
adapters live under `src/state/` and use `scr::state` namespaces. Native
race orchestration and replay I/O are shared by the player and development tools.
Application components use `scr::runtime`; recorded playback is one consumer of
the same `RaceScene`, presentation poses and `SoftwareRenderer` used for live racing.
Includes name their owning domain, such as `physics/car_dynamics.h` and
`presentation/race_render_assets.h`, without compatibility forwarding headers.
Core calculations use typed native values, while the compatibility memory layout
preserves recovered byte/word semantics. `tests/support/` imports these adapters
into the historical test namespaces and owns the oracle ranges and comparison
projections. Production targets have no test-support includes or Musashi linkage.

## Startup and race lifetime

The Windows host first creates the windowed launcher without a game session.
Native ADF import validates the supported disk and atomically replaces its local
pack, with source hashes and extraction offsets in the adjacent provenance file.
Play saves staged preferences and only then constructs NativeGameSession; import,
preference-write and initialization failures remain recoverable in the launcher.
Normal paths live in per-user Windows application data; development arguments
override them explicitly. Recording requires an explicit directory and does not
change simulation advancement or the initial presentation snapshots.

1. `local_asset_pack` validates the pack's identity and extents.
2. `startNativeGame` and `initializeNativeAssets` rebuild graphics, fonts, sample
   banks and initial game data in fresh memory.
3. `NativeOpeningFlow`, `NativePlayerNameEntry` and the opening renderer provide
   mode selection, names and roster/standings. `NativeMenuFlow` then owns the
   original menus, Practice and championship routes.
4. `NativeGameSession` prepares the selected track, preview and race context.
   `initializeNativeRace` constructs fresh fuel, damage, timing and placement;
   `RaceScene` runs the recovered gameplay step.
5. Race exit transfers persistent state back to the same menu session. Native
   career checkpoints retain the exact continuation stage and both RNG stores.
   A finished race stages its exit/scoring on a private copy before acknowledgement;
   restoration resumes the resulting report without repeating those effects.
   A shared Hall store observes eligible competitive records independently.
   Multiplayer manual progress uses the original codec and `NativeSaveStore`.

Single-player career selection always precedes name entry or opening a career.
Continue and Browse Careers open a three-row main menu. Resume Season restores
a separate suspended continuation, while Practice uses the main-menu state and
cannot consume the suspended season's RNG or advance its reports. Native
checkpoint version 2 can persist both states; version 1 remains readable.

The player enables [career persistence](components/native-career-persistence.md)
explicitly; legacy flow instances remain available to original-code regression
tests. A serial worker writes immutable payloads with flushed atomic replacement
and two valid recovery generations. Checkpoint capture, Hall observation and
restoration do not invoke the legacy RNG-consuming encoder. A per-data-directory
Windows mutex prevents simultaneous player writers; `--data-dir` isolates development.

Preview rendering owns a copy of track state. Race entry clears the menu text
mode before drawing the cockpit; it must not inherit menu displacement. Reset,
restart and menu return are separate transitions and must retain their original
RNG and persistence effects.

## Independent clocks and views

- Gameplay advances in fixed **120 ms** steps, about 8.33 updates per second.
- Recovered wheel and engine interrupt state advances at **50 Hz**. The native
  mixer outputs **48 kHz stereo** through a separate Windows audio worker.
- The host targets **60 presentations per second**. Original-update presentation
  holds gameplay poses; optional interpolation blends completed poses with one
  physics tick of live presentation latency.
- Original menu navigation uses recovered opening/menu delays and palette
  transitions, independently of the driving timestep. Responsive navigation is
  the player default; it removes these deliberate waits and completes the same
  transition handlers without advancing simulation time.

Input is polled on the presentation loop and retained until a physics tick.
Original and Modern control presets select the same five digital bits before
the input latch; Modern uses triggers for throttle independently of stick
steering. Original retains recovered latched acceleration. Modern gas follows
the current held input at the physics tick and clears the acceleration latch
before recovered control processing when gas is released. This optional input
policy produces coasting with zero engine force; it leaves the fixed-point
physics, AI, RNG and tick scheduling unchanged. The host passes the preset even
while gameplay input is suppressed after settings, so Modern cannot inherit an
old throttle latch. Menu navigation is independent of the driving preset.
Presentation stalls use bounded catch-up with fixed steps. Pause/settings suspend game and audio advancement,
preserve the fractional physics tick and require held controls to be released
before gameplay resumes.

`RaceScene` retains immutable presentation snapshots, bridge geometry and
renderer history. Interpolation and `renderView(CameraViewSettings)` rebuild
views from private copies; repeated or reverse queries must not alter later
physics, RNG, frames or saved state. Relative yaw/pitch/roll and cockpit visibility
are developer APIs, without a player camera selector. The world uses one recovered
far/near painter in Practice and competitive races; opponent insertion depends
on race mode.

The **4:3 (Original)** and **4:3 (Extended)** modes retain their **320×200**
bitmap and original display proportions. Original renders its **256×128**
world viewport; Extended reveals scenery behind the frame across **320×144**.
**16:9** uses a **432×200** working canvas, centring the same-size original car
and main arch at offset (56,0). Only the side braces and blue interior are
extended; the outer 14 columns of each brace are straightened to meet those
extensions. All car objects, hardware wheels, effects, instruments and message
panels use their existing 320×200 compositors, then translate by 56 pixels.
In wide views the upper cage is composited separately at 25% opacity, becoming
fully opaque above the highest wheel crop. Outer braces use 65% opacity, fading
back to solid at their inner joins. Tyre guards, blue panels and car pixels stay solid.
The wider world fills every exposed corner, with unchanged projection scale
and private near/far clipping bounds [-88,344] × [-16,184].
Host aspect correction and a symmetric outer crop present that working canvas
at exactly 16:9, preserving the car's scale at the same display height. Menus,
track previews and persisted finish images retain 4:3. No generated artwork
is loaded. Window shape alone does not change projection. See the
[view contract](components/native-extended-view.md).

**32:9** extends that same procedural frame over an **864×200** canvas, with
the original car/arch at offset (272,0) and near/far bounds [-304,560] × [-16,184].
Its centred 432 columns retain the 16:9 frame exactly. No new or edited car
artwork is needed; host cropping and pixel proportions keep the same scale at
the same display height. Menus and persisted finish images still use 4:3.

## Fidelity contract

Gameplay changes must preserve original arithmetic, operation order, wraparound,
RNG consumption and transition behaviour. Original/native differential tests run
both states independently; original outputs must never reseed the native side.
Claims are bounded by the declared memory projections, scenarios and harness
seams. Finite passing tests do not establish exhaustive whole-game equivalence.

Original-code checks cover menu/preview/cockpit pixels and many geometry,
visibility and draw-command paths. The world rasterizer has native line/polygon
coverage rules. Universal Amiga world-pixel identity, custom-chip DMA/blitter
timing, analog audio filtering and cycle-exact host input/display timing are not
claimed. Busy-loop delays use measured PAL timing; bus contention, rendering cost
and incidental idle-poll RNG advances are not reproduced.

The faithful gameplay remains the baseline. Menu navigation defaults to
Responsive, with Original separately selectable from the driving preset.
The host-time input clock emits fresh Confirm/Back actions and timed direction
repeats; explicit parent routes restore selections. Preview revisits reuse
prepared state, and championship reports keep their one-way progression.
See the [menu navigation design](design/menu-navigation-design.md) for routes
and cancellation boundaries. Future aspect, camera, filter and
graphics options belong in presentation and should preserve that baseline as a
selectable behaviour. Computer Link remains explicitly deferred. See
[presentation boundaries](components/native-presentation.md) and
[verification](development.md#choose-verification-by-change) before extending it.

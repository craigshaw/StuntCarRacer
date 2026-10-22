# Native architecture

The game is ordinary compiled C++17 inside a Windows event loop, with native
input, audio and software rendering. Original 68000 instructions run only in
separate verification executables. The disk-derived pack supplies data to native
decoders; retaining original code bytes in the pack does not make them executable.

## Runtime ownership

| Layer | Main sources | Responsibility |
| --- | --- | --- |
| `scr_physics` | `src/` gameplay modules | Fixed-point physics, controls, collision, AI, race timing, restart and bridge state |
| `scr_audio` | `src/engine_audio.*`, `paula_mixer.*` | Recovered engine interrupt, sample handlers and four-channel mixing |
| `scr_render` | `src/` geometry, projection, planar and raster modules | Original assets, geometry and native image generation |
| `scr_startup` | `app/native_startup.*`, `native_*` components | Asset/race initialization, original menus, text, records and competition rules |
| `scr_game` | `app/native_game_session.*`, `replay_scene.*` | Session/menu/race handoff, independently evolved gameplay, snapshots and recordings |
| `scr_display` | `app/native_launcher.*`, `windows_launcher.*`, `native_display_settings.*`, `windows_game_display.*` | Pre-game setup, display/control UI, scaling, fullscreen and preferences |
| Windows host | `app/native_game.cpp`, `windows_drive_input.*`, `windows_audio_output.*` | Event/presentation loop, keyboard, XInput, device playback and window lifecycle |

`CMakeLists.txt` owns production library linkage; `cmake/NativeTests.cmake` and
`cmake/OracleTests.cmake` define separate test targets. Presets share the native
build across suites, keep oracle output separate and stage only the player in
`dist/`. Production
state adapters currently reuse headers under `tests/`; the directory name does
not imply a runtime CPU dependency. Core calculations use typed native values,
while the compatibility memory layout preserves recovered byte/word semantics.

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
   `ReplayScene` runs the recovered gameplay step.
5. Race exit transfers persistent state back to the same menu session. Career,
   multiplayer, records and the original save/Replay codecs retain their own
   recovered rules. `NativeSaveStore` replaces floppy transport with atomic local
   file storage.

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
- Opening/menu delays and palette transitions use their recovered timing rules,
  independently of the driving timestep.

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

`ReplayScene` retains immutable presentation snapshots, bridge geometry and
renderer history. Interpolation and `renderView(ReplayViewSettings)` rebuild
views from private copies; repeated or reverse queries must not alter later
physics, RNG, frames or saved state. Relative yaw/pitch/roll and cockpit visibility
are developer APIs, without a player camera selector. The world uses one recovered
far/near painter in Practice and competitive races; opponent insertion depends
on race mode.

The source image is **320×200**, with a **256×128** world viewport. Host fitting,
bilinear scaling and settings UI happen after game rendering. Enlarging a window
does not change the projection or field of view.

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

The faithful game is the default baseline. Future aspect, camera, filter and
graphics options belong in presentation and should preserve that baseline as a
selectable behaviour. Computer Link remains explicitly deferred. See
[presentation boundaries](native-presentation.md) and
[verification](development.md#choose-verification-by-change) before extending it.

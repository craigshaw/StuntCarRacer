# Native architecture

The player is compiled C++17 in a Windows event loop, with native input, audio
and software rendering. The disk-derived pack supplies data to native decoders.
Its retained original code bytes are never executed; 68000 execution belongs
only to separate verification executables.

## Runtime ownership

| Layer | Main sources | Responsibility |
| --- | --- | --- |
| `scr_simulation` | `src/physics/`, `race/`, `track/`, sample requests in `audio/` | Fixed-point physics, controls, collision, AI, race rules, restarts and bridge state |
| `scr_audio` | `src/audio/engine_audio.*`, `paula_mixer.*` | Engine interrupt state and four-channel mixing |
| `scr_render` | `src/render/` | Geometry, projection, planar decoding and rasterization |
| `scr_state` | `src/state/` | Recovered-layout adapters, race-stage order, checkpoint/replay I/O |
| `scr_runtime` | `app/assets/`, `menus/`, `presentation/`, shared rule adapters | Initialization, original menus/text/screens and competition helpers |
| `scr_game` | `app/session/`, `race/`, `persistence/`, `input/`, `audio/` | Session handoff, career storage, gameplay orchestration, snapshots and recordings |
| `scr_host_ui` | `app/launcher/`, Windows launcher/display adapters | Setup, preferences, overlays, image scaling and fullscreen |
| Windows host | `app/main.cpp`, `app/platform/windows/` | Window/event loop, keyboard, XInput and audio device lifetime |

Application components use `scr::runtime`; core algorithms use typed values in
`scr`, and recovered-layout adapters use `scr::state`. Domain-qualified includes
identify ownership without forwarding headers. The compatibility layout preserves
original byte/word semantics but is constructed by native code. `tests/support/`
owns oracle ranges, comparison projections and historical test namespaces;
production neither includes those headers nor links Musashi.

`RaceScene`, presentation poses and `SoftwareRenderer` are shared by live racing
and development replay tools. CMake defines production linkage; `cmake/DevTools.cmake`
and the Native/Oracle test modules define optional tools and isolated test targets.
Only the player and licence notices are installed. See [development](development.md)
for build trees, fixture requirements and staging.

## Startup and race lifetime

1. The [launcher](components/native-launcher.md) opens before a game session.
   Native import validates the ADF and replaces a retained pack with hash/offset
   provenance. Play saves pending preferences before constructing the session;
   import, settings-write and initialization errors remain recoverable there.
2. `loadLocalAssetPack`, `startNativeGame` and `initializeNativeAssets` rebuild
   graphics, fonts, sample banks and fresh game data from the validated pack.
3. [Opening](components/native-opening.md) and menu flows own mode selection,
   names, roster/standings, Practice and championship routes. Single Player first
   selects a native career; legacy flows remain available to original-code tests.
4. `NativeGameSession` prepares the track/preview, then `initializeNativeRace`
   constructs fuel, damage, timing and placement. `RaceScene` advances gameplay.
   Preview owns a track-state copy; race entry clears menu text mode before drawing.
5. Race exit transfers persistent state to the same menu session. Restart, reset
   and menu return are distinct transitions with their original RNG effects.

[Native checkpoints](components/native-career-persistence.md) retain explicit
continuation stages and both RNG stores without invoking the RNG-consuming legacy
save encoder. Continue opens the career's main menu; Resume Season restores an
independent suspended-season state. Practice cannot advance or replace it.
A finished race stages exit/scoring on a private copy so restoring an outstanding
finish/report does not repeat those effects. Eligible Hall records persist
independently; multiplayer manual progress retains the original codec.

A serial worker writes immutable persistence jobs with flushed atomic replacement
and two recovery generations. Capture, observation, restoration and storage never
advance simulation. A per-data-directory Windows mutex prevents conflicting player
writers; `--data-dir` isolates development instances.

## Independent clocks and views

| Clock | Rate / contract |
| --- | --- |
| Gameplay | Fixed 120 ms updates; never enlarged to catch up |
| Wheel/engine interrupt state | 50 Hz |
| Audio output | 48 kHz stereo on a separate Windows worker |
| Host presentation | Targets 60 frames per second |

Input is polled by the presentation loop and latched until gameplay consumes it.
Both driving presets map to the same five digital bits. Original retains latched
acceleration; Modern clears that latch before recovered control processing when
gas is released, producing coasting without changing fixed-point physics, AI or
RNG order. The preset still applies while post-settings input is suppressed.

Original menu navigation preserves recovered waits/fades. Responsive navigation
uses host-time fresh actions, direction repeat and explicit Back routes through
the same transition handlers, without advancing gameplay. Navigation is independent
of driving controls; [menu flow](components/native-menu-flow.md) defines its routes.

Presentation stalls use bounded fixed-step catch-up. Pause, Settings, the Game
menu and quit confirmation suspend game/audio advancement, preserve the fractional
tick and require held inputs to be released before racing resumes.

`RaceScene` retains immutable frame/bridge snapshots and presentation history.
Optional interpolation blends completed poses with one gameplay tick of live
presentation latency. `renderView(CameraViewSettings)` uses private copies;
repeated or reverse queries cannot alter later physics, RNG, frames or saves.
Relative camera offsets and cockpit visibility are development APIs.

Original/Extended 4:3 and wide 16:9/32:9 presentation keep the original car's scale
at a fixed display height. World clipping, aspect correction, frame extension and
filtering remain independent of simulation; menus, previews and saved finish images
remain 4:3. The [view contract](components/native-extended-view.md) owns geometry,
masking, opacity and crop details; [presentation](components/native-presentation.md)
owns interpolation and host overlays.

## Fidelity contract

Gameplay preserves arithmetic, operation order, wraparound, RNG consumption and
transition behaviour. Differential tests evolve original/native states independently;
original outputs never reseed native execution. Passing finite scenarios establishes
only their declared projections and harness boundaries, not exhaustive equivalence.

Original-code checks cover menu, preview and cockpit pixels plus many geometry,
visibility and ordered draw-command paths. The world rasterizer has native coverage
rules. Universal Amiga world-pixel identity, custom-chip DMA/blitter timing, analog
audio filtering and cycle-exact host input/display timing are not claimed. Busy
delays use measured PAL timing; rendering cost, bus contention and incidental idle
polling RNG advances are not reproduced.

The faithful baseline remains selectable. Future camera, aspect, filter and graphics
options belong to presentation; Computer Link remains deliberately deferred.
Use [verification by change](development.md#choose-verification-by-change) before
altering either boundary.

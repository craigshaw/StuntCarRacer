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

The launcher imports assets and saves pending preferences before constructing
`NativeGameSession`. Setup failures remain recoverable there. The session owns
menus, race preparation and return; `RaceScene` advances the race and captures
presentation frames. See [assets](components/assets.md) for initialization order
and [sessions](components/sessions.md) for progression and persistence boundaries.

## Independent clocks and views

| Clock | Rate / contract |
| --- | --- |
| Gameplay | Fixed 120 ms updates; never enlarged to catch up |
| Wheel/engine interrupt state | 50 Hz |
| Audio output | 48 kHz stereo on a separate Windows worker |
| Host presentation | Targets 60 frames per second |

Input is polled by the presentation loop and latched until gameplay consumes it.
Both driving presets use the same five digital bits; Modern clears the original
acceleration latch on gas release. Navigation policy is independent of driving.
Stalls use bounded fixed-step catch-up. Pause and overlays preserve the fractional
tick and require held input to be released before racing resumes.

Presentation consumes retained snapshots. Alternate views and interpolation use
private state and cannot affect later gameplay, RNG or saves. The
[presentation contract](components/presentation.md) covers history, views and audio;
[simulation](components/simulation.md) records the less obvious state dependencies.

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

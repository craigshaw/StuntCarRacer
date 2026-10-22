# Stunt Car Racer — native port

A native C++17 Windows port of the Amiga game, with recovered gameplay,
original local assets and a software renderer. The game starts from disk-derived
data and fresh native state. It requires no captured Practice state, Kickstart
ROM, WinUAE or CPU emulator at runtime.

The initial faithful port is the baseline for further work. Physics, AI, RNG,
race rules and state transitions retain original behaviour. Presentation can
evolve separately; universal pixel identity for the in-race world renderer is
not a release condition.

## Play

Open `dist/Stunt Car Racer.exe` to use the launcher after building and staging below.
Choose your supported original ADF on first use; the application validates and
imports its assets without Python or an emulator. Choose display, scaling,
motion and controls, then press **Play** to enter the original mode chooser.
The ADF remains unchanged and is not needed again after import.

On the prepared development machine, **[Drive Native.cmd](Drive%20Native.cmd)**
remains a provisional build/check helper. It opens the same launcher using the
checkout's existing assets, settings and saves.

Select Single Player League, enter your name, continue past the roster, then
choose Practise or Start the Racing Season. The game also supports two to eight
local players taking turns against the computer pace car.

| Action | Keyboard | Xbox / XInput controller |
| --- | --- | --- |
| Navigate / confirm | Arrows, Enter or Space | Stick/D-pad, A |
| Accelerate / brake | Up/Down or W/S | Original: stick/D-pad up/down; Modern: RT/LT |
| Steer / boost | Left/Right or A/D, Space | Stick/D-pad, A or RB |
| Pause | P | Start |
| Leave a race | Escape | Back/View |
| Settings | F10 | Start from menus; pause panel during races |
| Fullscreen / interpolation / mute | F11 / I / M | Settings for fullscreen and interpolation |

Settings offer saved **Original** (default) and **Modern** digital control presets.
Modern separates throttle from the steering stick; triggers are on/off.
Modern accelerates while gas is held and coasts on release; Original retains
latched acceleration until braking. The same rule applies to keyboard gas.
Text entry uses the original name editor; typing letters does not trigger driving shortcuts.
See [playing and settings](docs/native-game.md) for menu, save and controller details.

Normal launches store assets, preferences and saves under
`%LOCALAPPDATA%/Stunt Car Racer/`. The development helper retains
`working/settings/display.ini` and `working/saves/stunt-car-racer.scr-save`.
Settings provide control presets, borderless
fullscreen, integer/sharp/smooth scaling and optional motion interpolation.
Physics stays at **120 ms per update**, wheel/engine interrupts at **50 Hz**, and
host presentation targets **60 FPS**.

The staged executable is `dist/Stunt Car Racer.exe`.
To run an existing build directly, from the project root:

```powershell
& '.\dist\Stunt Car Racer.exe' `
  --asset-pack .\analysis\assets\stunt-car-racer.scr-assets
```

## Current scope

- Original startup, name entry, menus, overview and all eight tracks, including Draw Bridge.
- Practice, opponent races, both leagues, career progression and championship endings.
- Two-to-eight-player local championships, results, damage and track bonuses.
- Hall of Fame, thirty save slots, career/record/multiplayer saves and original Replay flow.
- Cockpit instruments, wheel animation, engine sound and effects, keyboard and XInput input.
- ADF setup launcher, fullscreen, saved preferences and optional smooth presentation.

**Computer Link is deliberately deferred.** Widescreen field of view, alternate
cameras, graphical enhancements and distribution
packaging are future work. Fullscreen currently fits the original 320×200 image;
it does not widen the view. The native rasterizer, audio output and host timing
have [documented fidelity boundaries](docs/architecture.md#fidelity-contract).

## Develop or continue in another thread

Read [AGENTS.md](AGENTS.md), then [development and verification](docs/development.md)
and [architecture](docs/architecture.md). The [documentation index](docs/README.md)
links the component contracts and original-code evidence. These describe current
behaviour; generated receipts under ignored `analysis/` identify what was
actually checked and against which sources.

The prepared Windows toolchain uses CMake, Visual Studio C++ Build Tools and
`C:/Tools/amiga-re/venv/Scripts/python.exe`. A source checkout does not include
game assets, test captures, third-party verification tools or built executables.
See the development guide before setting up another machine. Build, test, stage
and run are separate steps:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows --config Release
& '.\dist\Stunt Car Racer.exe'
```

Release and Debug share `build/windows/`; original-code tests use
`build/windows-oracle/`. `dist/` contains only the player executable. The
existing private references and check reports remain under `analysis/`.
See [build options and layout](docs/development.md#build-test-and-stage).

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_game.py
# With the local original-code verification prerequisites prepared:
& $AmigaPython scripts/check_native_game.py --verify-original
```

## Local assets and reference tools

The application validates one supported ADF by hash and retains an asset pack plus
provenance manifest. Development import tooling writes these under `analysis/assets/`.
Native code decodes graphics, audio and track
data and constructs fresh game state from that pack. The supplied disk remains
untouched. [Asset format and safeguards](docs/native-startup.md) describe the pipeline.

Only source, extraction recipes, hashes, documentation and authored tests belong
in Git. Original media, packs, saves, captures, derived images/audio, binaries and
analysis databases stay local. `.gitignore`, the pre-commit hook and source-only
CI enforce common asset exclusions; enable the hook in each checkout:

```powershell
git config core.hooksPath .githooks
```

- **[Play Stunt Car Racer.cmd](Play%20Stunt%20Car%20Racer.cmd)** boots the original
  ADF in the prepared WinUAE profile. F12 opens emulator settings.
- **[Watch Native Replay.cmd](Watch%20Native%20Replay.cmd)** verifies and presents
  the retained three-lap authored replay; it needs the local capture fixtures.
- `scripts/start-native-race.ps1 -Track 5` opens a fresh direct Practice race for
  development, bypassing menus.

These launch paths are distinct. See [reverse-engineering tools and mapping](docs/reverse-engineering.md)
for WinUAE controls, raw 68000 analysis and retained evidence locations.

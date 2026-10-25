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

Select Single Player League to see New Career, plus Continue for the available
last-used career and Browse Careers when saved careers exist. New Career asks
for a name and confirms the roster, then saves automatically. Continue and Browse
open the three-row single-player menu: Hall of Fame, Practise and Start Season
or Resume Season. The game also supports two to eight
local players taking turns against the computer pace car.

| Action | Keyboard | Xbox / XInput controller |
| --- | --- | --- |
| Navigate / confirm | Arrows, Enter or Space | Stick/D-pad, A |
| Back in menus (Responsive) | Escape | B |
| Accelerate / brake | Up/Down or W/S | Original: stick/D-pad up/down; Modern: RT/LT |
| Steer / boost | Left/Right or A/D, Space | Stick/D-pad, A or RB |
| Game menu | Esc during racing or at the mode chooser; P outside name entry | Start |
| Leave a race | Game menu → Leave Race | Game menu → Leave Race; Back/View shortcut |
| Quit to desktop | Game menu → Quit to Desktop | Game menu → Quit to Desktop |
| Settings | F10; Game menu → Settings | Game menu → Settings |
| Fullscreen / interpolation / mute | F11 / I / M | Settings for fullscreen and interpolation |
| Toggle FPS counter | F | — |

Settings offer saved **Original** (default) and **Modern** digital control presets.
Modern separates throttle from the steering stick; triggers are on/off.
Modern accelerates while gas is held and coasts on release; Original retains
latched acceleration until braking. The same rule applies to keyboard gas.
Text entry uses the original name editor; typing letters does not trigger driving shortcuts.
Menus default to **Responsive** navigation: immediate input, held-direction
repeat and Back one level with remembered selections. Choose **Original** menu
navigation in the launcher or Settings for the faithful timing and routes.
This setting is independent of the driving preset. In a single-player career,
Back from a matchup or report returns to that career's main menu while preserving
the exact pending stage. Resume Season restores it; A/Enter advances reports.
Back from the main menu returns to career selection, then to the mode chooser.
Practice preserves a suspended season. Existing Cancel entries remain available.
See [playing and settings](docs/playing.md) for menu, save and controller details.

Normal launches store assets, preferences and saves under
`%LOCALAPPDATA%/Stunt Car Racer/`. The development helper retains
`working/settings/display.ini` and `working/saves/stunt-car-racer.scr-save` for
multiplayer manual saves, with careers and shared Hall in `working/player-data/`.
Settings provide control presets, borderless
fullscreen, Integer/Fit/Stretch sizing, separate filtering and optional 60 FPS
smoothing for camera/cockpit movement, wheel placement, dust/spark motion and
lifting chains. Integer Auto is the default; fixed 1×–32× scales are also available.
**Aspect ratio** offers **4:3 (Original)** (default), **4:3 (Extended)**,
**16:9** and **32:9** in the launcher and Settings. Extended reveals scenery in the black
upper corners outside the original arch. Both wide modes add a wider world around the
same-size car and main arch, extending only the side braces and blue interior.
Both wide modes render the upper cage and outer side braces translucently,
retaining solid inner posts and blue side panels around the original tyres.
The original engine, wheels, exhausts and instruments keep their pixels and
scale. Menus and saved career finish images retain their original 4:3 format.
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
- Independent single-player careers, Continue, automatic checkpoints and shared Hall records.
- Thirty manual multiplayer save slots and its original Replay flow.
- Cockpit instruments and running lap hundredths, wheel animation, engine sound and effects, keyboard and XInput input.
- ADF setup launcher, fullscreen, saved preferences and optional smooth presentation.

**Computer Link is deliberately deferred.** Alternate cameras, further graphical
enhancements and distribution packaging are future work. Fullscreen fits the
selected 4:3, 16:9 or 32:9 view; image sizing and filtering remain independent.
The native rasterizer, audio output and host timing
have [documented fidelity boundaries](docs/architecture.md#fidelity-contract).

## Develop or continue in another thread

Read [AGENTS.md](AGENTS.md), then [development and verification](docs/development.md)
and [architecture](docs/architecture.md). The [documentation index](docs/README.md)
links the component contracts and original-code evidence. These describe current
behaviour; generated receipts under ignored `analysis/` identify what was
actually checked and against which sources.

The Windows build uses CMake and Visual Studio C++ Build Tools. Python is an
optional development/check tool. A source checkout does not include
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
See [local tool configuration](setup/README.md) when setting up development helpers.

Gameplay algorithms live in `src/`, shared state adapters in `src/state/`, and
the player application in `app/`. Optional replay tools live in `tools/replay/`;
`tests/` separates unit, integration and original-code oracle checks.

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
untouched. [Asset format and safeguards](docs/components/native-startup.md) describe the pipeline.

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

These launch paths are distinct. See [reverse-engineering tools and mapping](docs/research/reverse-engineering.md)
for WinUAE controls, raw 68000 analysis and retained evidence locations.

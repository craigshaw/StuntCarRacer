# Stunt Car Racer — native port

A native C++17 Windows port of the Amiga game, with recovered gameplay, a software
renderer and optional modern presentation. It starts from fresh native state and
data imported from your own supported ADF. No Kickstart ROM, WinUAE, captured
Practice state or CPU emulator is needed to play.

Physics, AI, RNG and race rules retain original behaviour. Gameplay updates stay
at 120 ms, wheel/engine state at 50 Hz, and host presentation targets 60 FPS.
Rendering and input options preserve a selectable faithful baseline; see the
[fidelity contract](docs/architecture.md#fidelity-contract).

## Play

After building and staging below, open `dist/Stunt Car Racer.exe`:

1. Choose your supported original ADF. One verified disk revision is supported;
   [the asset guide](docs/components/native-startup.md) lists its identity.
2. Select display and control settings, then press Play.
3. Choose Single Player League to create/continue a career, or Multiplayer for
   two to eight local drivers taking turns against the computer pace car.

The ADF stays unchanged and is not needed after import. Normal assets, settings
and saves live under `%LOCALAPPDATA%/Stunt Car Racer/`.

Drive with arrows/WASD and Space for boost, or an XInput controller. Original
controls latch acceleration; Modern uses held gas and separate controller triggers.
Escape/Start opens the Game menu while racing; F10 opens Settings. See
[playing and settings](docs/playing.md) for controls, menu routes, saves and recovery.

## Current scope

- Original opening, menus, all eight tracks including Draw Bridge, and both leagues.
- Practice, opponent races, championship progression and endings.
- Independent single-player careers, autosave, Resume Season and shared Hall records.
- Two-to-eight-player local championships and thirty manual multiplayer save slots.
- Original cockpit instruments/effects, running lap hundredths, wheel animation and audio.
- Original/Extended 4:3, 16:9 and 32:9 views, preserving the original car's scale.
- Saved display/filter/control preferences, fullscreen and optional 60 FPS smoothing.
- Responsive menu navigation with a separately selectable Original policy.

Computer Link is deliberately deferred and disabled in the chooser. Alternate
player cameras and further graphical enhancements remain outside the current scope.

## Build

Install CMake and the Visual Studio C++ x64 tools described in
[development](docs/development.md#environment), then run from the repository root:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows --config Release
& '.\dist\Stunt Car Racer.exe'
```

Release/Debug share `build/windows/`; original-code verification uses a separate
tree. CTest skips checks whose private fixtures are absent. Python is optional
for development checks, not a player dependency. Install stages the EXE and
licence notices in `dist/` without game-data packs.

[Drive Native.cmd](Drive%20Native.cmd) remains a provisional build/check helper
using checkout-local assets and saves. Ordinary executable launches use per-user data.

## Development and assets

Start with [development and verification](docs/development.md),
[architecture](docs/architecture.md) and the [documentation index](docs/README.md).
[AGENTS.md](AGENTS.md) records contributor constraints; [scripts](scripts/README.md)
and [development tools](tools/README.md) distinguish player, direct-race and replay paths.

Only source, extraction recipes, hashes, documentation and authored test inputs
belong in Git. ADFs, ROMs, packs, saves, captures, generated media, binaries and
analysis databases stay local. The asset checker, optional pre-commit hook and
source-only CI guard against common accidental additions. See
[asset safeguards](docs/components/native-startup.md#source-control-safeguards)
and [release checks](docs/development.md#build-test-and-stage).

## Credits and licence

Original game: **Geoff Crammond / MicroStyle (1989)**.
Research used [Vesuri's disassembly](https://github.com/Vesuri/stuntcarracer);
optional original-code tests use [Musashi](https://github.com/kstenerud/Musashi).

Original contributions to this port use the [MIT License](LICENSE).
The original game and third-party material retain their own rights; see
[third-party notices](THIRD_PARTY_NOTICES.md). No ADF, ROM or extracted game-data
pack is included. The launcher logo is an explicit branding exception.
This project is unofficial and unaffiliated with the original game's rights holders.

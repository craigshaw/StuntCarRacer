# Development and verification

## Start a new thread here

Read [AGENTS.md](../AGENTS.md), the [README](../README.md) and
[architecture](architecture.md). The initial local game is the faithful baseline;
enhancements should be explicit presentation options. Computer Link is deliberately
deferred. No enhancement work is implicitly scheduled by this handoff.

Inspect `git status` and the relevant sources before editing. Work may be
uncommitted; preserve it. Read the matching [component guides](README.md), then
select verification appropriate to the change. Do not repeat disk extraction
discovery or replace original behaviour with the enhanced upstream disassembly.

## Development environment

Commands below run from the project root in PowerShell.

| Requirement | Prepared location / version |
| --- | --- |
| C++17 compiler | Visual Studio 2026 C++ Build Tools; CMake generator `Visual Studio 18 2026`, x64 |
| CMake | 4.4.3 on PATH; project/preset minimum 3.21 |
| Python | Optional for verification/helpers; local configuration, `.venv`, or PATH |
| Python RE packages | Versions in `setup/requirements.txt` |
| Local disk | Supported ADF, identity in [asset guide](components/native-startup.md) |

PowerShell helpers read the ignored `setup/local-tools.json`, then environment
variables and installed commands. This checkout's existing tool paths are saved
there. Copy `setup/local-tools.example.json` on another machine and fill only the
overrides needed; see [local tool configuration](../setup/README.md).
`scripts/env.ps1` exposes `$AmigaPython`, `$WinUae` and `rea-wsl` without requiring
the research environment to be installed. Native Windows UI/audio/input use system libraries, including XInput;
the game executable imports ADFs itself and has no Python or emulator runtime
dependency. Release builds statically link the Visual C++ runtime. The supplied
launchers do use Python for import, build and checks.

A clone supplies source only. Copy your own supported ADF locally, or an already
imported matching pack. ROMs and captures are unnecessary for ordinary native
startup. Original-code oracles and capture-based replay need additional local
fixtures, described below.

## Build, test and stage

From the project root:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows --config Release
& '.\dist\Stunt Car Racer.exe'
```

The presets select the prepared x64 Visual Studio toolchain. Configure generates
the project, Build compiles it, CTest runs existing test executables, and Install
stages the player executable. Running the staged game performs no build or tests.
The default install prefix is `dist/`; it contains only `Stunt Car Racer.exe`.
The disk's asset pack is imported by the player on first launch and is not
bundled there. Supplied local header artwork remains embedded in the executable.

| Path | Purpose |
| --- | --- |
| `src/` | Typed gameplay, audio and rendering algorithms, with shared recovered-layout adapters in `src/state/` |
| `app/` | Player session, menus, launcher and Windows host |
| `tools/replay/` | Optional replay viewer and headless native replay entry points |
| `scripts/`, `tools/research/` | Everyday checks/launchers and optional recovery, emulator and reference-analysis utilities |
| `setup/local-tools.json` | Ignored per-machine tool paths; the checked-in example contains no machine paths |
| `tests/unit/`, `tests/integration/`, `tests/oracle/` | Asset-free checks, native integration and test-only original-code comparisons |
| `tests/support/`, `tests/replays/` | Test-only adapters/projections and authored replay inputs/provenance |
| `CMakePresets.json`, `cmake/` | Shared toolchain settings, native/oracle target definitions and CTest registration |
| `build/windows/` | Shared native compiler output for Release and Debug, CTest logs and per-configuration test outputs |
| `build/windows-oracle/` | Separate original-code test build, including test-only Musashi |
| `dist/` | Staged player executable |
| `analysis/` | Private inputs, recovered references, research evidence and detailed verification reports |
| `working/` | Development settings, saves and emulator working copies |

All generated/local directories are ignored. Historical executables and hash
receipts remain under `analysis/`; their obsolete CMake projects and compiler
intermediates have been removed. Current commands build only under `build/`.
The gameplay algorithms retain their implementations. Shared state adapters
belong to production source; test-only comparison ranges remain under
`tests/support/`. Research evidence and its existing paths are unchanged.
The [evidence map](research/evidence.md) identifies the retained data categories.

Debug outputs and generated CTest artifacts are disposable. Rebuild Debug with
its preset after cleaning it; CTest recreates its own output directories. Keep
original inputs, captured fixtures, references, research exports and identified
verification results. On this Windows checkout, retained `analysis/` data uses
transparent Windows compression: file contents, hashes and paths are unchanged.

Use `windows-debug` for Configure, Build and CTest to select Debug in the same
native tree. MSVC Debug executables reserve an 8 MiB stack for unoptimized
native memory/snapshot copies; Release retains its existing stack setting.
For an app-only build, `cmake --build --preset windows-release
--target native_game` skips test/viewer targets. A custom configure can disable
their definitions with `-DBUILD_TESTING=OFF -DSCR_BUILD_DEV_TOOLS=OFF`.
`SCR_BUILD_ORACLE` defaults to OFF and requires `BUILD_TESTING=ON` when enabled.

CTest includes synthetic input/audio/storage tests and native integration tests
for startup, menus, races, saves, display and launcher behaviour. Tests needing
private files are explicitly **Skipped** when those files are absent. Import a
pack with the development tool below, then rerun CTest to include them. Override
`SCR_TEST_ASSET_PACK`, `SCR_TEST_ADF` (optional) and `SCR_TEST_PAYLOAD` at configure
time when local inputs live elsewhere. `ctest --preset windows-release -L unit`
runs the asset-free native subset. Python source-policy tests are registered
when an interpreter is available; compiling the application does not require it.
Native window and host-storage checks run serially; other tests can run in parallel.
Rapid repeated persistence checks can expose intermittent Windows access-denied
errors during atomic file replacement. These remain test failures with a Windows
error diagnostic; this build cleanup does not change runtime write/retry behaviour.

CTest writes logs under `build/<tree>/Testing/` and test artifacts under
`build/<tree>/test-output/<configuration>/`, resetting each suite's own output
before it runs. It does not import assets or refresh
the detailed wrappers' source/fixture/executable receipts. Those wrappers remain
the authority for retained differential evidence and run against the shared
Release builds. Run one build/check wrapper at a time against a shared tree.
Source relocations invalidate earlier build receipts; rerun the relevant wrapper
before using `--skip-build`. Historical reports retain their recorded identities.

Python unit tests also use the build's test-output directory, with unique
temporary folders per test. Direct Python runs default to
`build/windows/test-output/python/`; `SCR_TEST_OUTPUT_ROOT` overrides that base.
CTest registers the asset-policy, replay-wrapper and lap-evidence Python suites.

After preparing the original-code prerequisites, the equivalent oracle commands
are `cmake --preset windows-oracle`, `cmake --build --preset windows-oracle`, and
`ctest --preset windows-oracle`. This also registers capture-free original-code
comparisons; deeper capture/replay checks still use the focused wrappers below.

On a machine with another supported generator, use a local ignored
`CMakeUserPresets.json` inheriting these presets, or configure CMake explicitly.
The supplied Python wrappers target the checked-in Windows Release presets.

## Development helpers and detailed receipts

```powershell
. .\scripts\env.ps1
git config core.hooksPath .githooks
& $AmigaPython scripts/import_native_assets.py --adf 'C:/path/to/game.adf'
& $AmigaPython scripts/check_native_game.py
.\scripts\start-native-game.ps1 -SkipBuild
```

`-SkipBuild` still runs checks and rejects a stale source/executable receipt.
Omit it after source changes. Passing `-Adf <path>` to the launcher imports that
disk; otherwise a missing pack is imported from the default root ADF filename.
Packs must remain under ignored `analysis/`.

These checkout-local paths belong to development commands. A normal executable
launch uses `%LOCALAPPDATA%/Stunt Car Racer/` and opens the setup/settings launcher
even without an asset pack. `--asset-pack`, `--settings-file` and `--save-file`
override individual paths; `--recordings-dir` opts into development recordings.
Drive Native.cmd remains a provisional helper, independent of the player app.

The supplied local `logo.png` is ignored and embedded as a native resource when
present at configure/build time. It is never copied into Git; a source-only clone
builds a text header until the local artwork is supplied. The native game receipt
records its hash and rejects stale artwork with `--skip-build`.

`check_native_game.py` stages the checked executable in `dist/` after successful
verification, verifies the copy's hash, and records both build and staged paths.
Drive Native.cmd launches that staged executable with development data. Each
suite using a build receipt keeps it beside its reports in `analysis/`, so
shared compiler output cannot overwrite another suite's receipt. Source hashes
include the presets and CMake modules. `--skip-build` continues to reject stale
sources or changed executables; a bare CMake build does not create a wrapper receipt.

## Choose verification by change

Use `& $AmigaPython scripts/<command>` after sourcing `env.ps1`.

| Changed area | Command | Main receipt directory |
| --- | --- | --- |
| Game flow, settings, startup integration, persistence | `check_native_game.py` | `analysis/native-game/` |
| Same routes against original code, career/multiplayer rules and screens | `check_native_game.py --verify-original` | `analysis/native-game/` |
| Fresh track/race state and steering | `check_native_startup.py --verify-original` | `analysis/native-startup/` |
| Opening, raw keyboard/name editor, startup pixels | `check_native_opening.py` | `analysis/native-opening/` |
| Cockpit pixels/events, wheel state, first/repeated races, three-lap fixture | `check_native_cockpit.py` | `analysis/native-cockpit/` |
| Opponent AI, contact and car painter | `check_native_opponents.py` | `analysis/native-opponents/` |
| World rendering, view isolation, both leagues/all tracks | `check_native_render_audit.py` | `analysis/render-audit/` |
| Recorded replay, interpolation, input clock and offline audio | `check_native_rendering.py` | `analysis/native-render/` |
| Fixed-point physics driver and supporting stages | `check_physics_update.py` | `analysis/physics-update/` |
| Track decoder and initialization | `check_native_track_initialization.py` | `analysis/native-track-init/` |

Run the focused checks required by the actual change; there is no need to rebuild
every historical leaf suite for a documentation or host-only edit. Physics and
gameplay changes require relevant original-code differential checks. Presentation
changes require evidence that view queries leave simulation state intact, plus
visual inspection where appearance changes. Interactive display/device behaviour
also needs an appropriate manual check.

Most wrappers write `check-results.json`, build logs and a source/executable
receipt under their directory. Read the result and verify its source, input,
asset and executable hashes before relying on a retained pass. A file existing
or a build succeeding is not evidence that comparisons ran. A later ordinary
game check can replace the combined report with one lacking original comparisons;
inspect the report's contents, not just `status`. Startup retains its deeper
original report separately as `original-check-results.json`.

## Original-code and replay prerequisites

The prepared checkout retains the matching payload, pinned reference material
and captured fixtures under `analysis/`. Original-code wrappers use the payload
at `analysis/reference/vesuri/offline-extracted-payload.bin` and pinned Musashi
source under `analysis/reference/musashi/`. Verify or fetch the latter with:

```powershell
& $AmigaPython scripts/prepare_wheel_oracle.py
# Only when the pinned files are missing:
& $AmigaPython scripts/prepare_wheel_oracle.py --download
```

Musashi is pinned by `setup/musashi-source-lock.json`. Tools do not normally
download it during a check. [Reverse-engineering setup](research/reverse-engineering.md)
describes the verified payload mapping, retained reference prerequisites and
capture extraction. The old capture-based wrappers are development tools, not
requirements for playing the native game. Cockpit/replay checks additionally use
the retained three-lap capture fixture and its authored control provenance.

Do not compare a fresh interactive drive against the default captured Practice
seed. A valid differential replay needs the exact race-entry state and controls
for that drive. Interactive recordings currently write controls and native traces,
without exporting that seed. Session tests explicitly export seeds for their own
recorded races; see [recordings](components/native-driving.md#recordings-and-verification).

## Repository hygiene and handoff

```powershell
& $AmigaPython scripts/check_repository_assets.py --worktree
& $AmigaPython -m unittest discover -s tests/unit -p 'test_*.py'
git diff --check
```

The asset check inspects indexed blobs and, with `--worktree`, non-ignored source.
CI currently runs the source asset policy and all Python synthetic tests. Native CTest
unit tests can run without private inputs; the full game tests need the local
pack. Never force-add original media, packs, saves,
screenshots, extracted data, binary builds or RE databases. Keep derived files in
`analysis/`, compiler/test output in `build/`, staged executables in `dist/`,
and working emulator/save/settings files in `working/`.

For a handoff, state what changed, which checks actually ran, any remaining
limitations and whether work is committed/pushed. Update the relevant guide when
its contract changes; keep the README and this entry path short. Do not turn
them into a running session log. Every Git commit carries:

```text
Co-authored-by: Codex <codex@openai.com>
```

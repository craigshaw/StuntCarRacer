# Development and verification

Read [architecture](architecture.md) for runtime ownership and fidelity limits,
then use the [component index](README.md) to select the relevant contract.
[AGENTS.md](../AGENTS.md) records contributor constraints. Commands below run
from the repository root in PowerShell.

## Environment

The player uses C++17 and Windows system libraries, including XInput. The checked-in
presets select x64 Visual Studio 2026 C++ Build Tools (`Visual Studio 18 2026`);
install its C++ workload and a CMake version supporting that generator. The project
and preset schema require CMake 3.21 or newer. Another generator can use an ignored
`CMakeUserPresets.json` or an explicit configure command.

Python is optional for checks/helpers and is not a compiler or runtime dependency.
The executable imports its own ADF and needs no ROM, emulator or capture. Release
builds statically link the Visual C++ runtime. Optional research dependencies are
pinned in `setup/requirements.txt`; see [local tools](../setup/README.md) for
discovery and ignored `setup/local-tools.json` overrides.

## Build, test and stage

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows --config Release
& '.\dist\Stunt Car Racer.exe'
```

Configure defines targets, Build compiles them, CTest runs existing executables,
and Install stages the player. Running the staged executable does none of those
steps; its launcher accepts the player's supported ADF on first use.

| Preset | Build tree | Configuration / purpose |
| --- | --- | --- |
| `windows-release` | `build/windows/` | Native Release |
| `windows-debug` | `build/windows/` | Native Debug; 8 MiB stack for unoptimized state copies |
| `windows-oracle` | `build/windows-oracle/` | Release with test-only original-code comparisons |

Use the same preset for configure, build and CTest. An app-only build uses
`cmake --build --preset windows-release --target native_game`. Custom configuration
can omit tests/viewers with `-DBUILD_TESTING=OFF -DSCR_BUILD_DEV_TOOLS=OFF`.
`SCR_BUILD_ORACLE` defaults to OFF and requires testing.

Install produces exactly `Stunt Car Racer.exe`, `LICENSE` and
`THIRD_PARTY_NOTICES.md` under `dist/`. Assets, settings and saves are not bundled.
The approved launcher logo is the sole embedded-artwork exception: a local
`logo.png` must match `setup/branding.json`. Unknown artwork fails configuration;
replacement requires provenance review and a pin update. Builds without it use
a text header; `-DSCR_EMBED_LAUNCHER_LOGO=OFF` selects that header explicitly.
Before distributing the folder, run:

```powershell
python scripts/check_release.py
```

This checks the file list, current notices, x64 executable and embedded resources.
Only version information, the Windows manifest and the exact approved logo are
accepted. The game wrapper also runs it after staging.

## Repository layout

| Path | Responsibility |
| --- | --- |
| `src/` | `physics/`, `race/`, `track/`, `audio/`, `render/`; recovered-layout adapters in `state/` |
| `app/` | Assets, input/audio, launcher, menus, persistence, presentation and race/session orchestration; Windows adapters in `platform/windows/` |
| `tests/` | `unit/`, `integration/`, test-only `oracle/` and `support/`, authored inputs in `replays/` |
| `scripts/`, `tools/` | Everyday checks/launchers; replay viewers and optional research recipes |
| `cmake/`, `CMakePresets.json` | Target definitions, test registration and shared build settings |
| `build/`, `dist/` | Disposable compiler/CTest output; staged player and notices |
| `analysis/` | Private media, recovered references and source/fixture/executable-identified evidence |
| `working/` | Checkout-local settings, saves and emulator working copies |

Generated/local directories are ignored. Preserve original media, captures,
reference provenance and identified verification reports; their locations are
indexed in [retained evidence](research/evidence.md). Production includes no test
headers and links no Musashi code.

## Development helpers and detailed receipts

```powershell
. .\scripts\env.ps1
git config core.hooksPath .githooks
& $AmigaPython scripts/import_native_assets.py --adf 'C:/path/to/game.adf'
& $AmigaPython scripts/check_native_game.py
.\scripts\start-native-game.ps1 -SkipBuild
```

`Drive Native.cmd` is a provisional build/check/launch helper. It uses checkout-local
assets and `working/` data; ordinary launches use per-user Windows data.
`-Adf <path>` imports through the helper; otherwise a missing pack uses the default
root ADF filename. Development imports stay under `analysis/`.

Native `--asset-pack`, `--settings-file` and `--save-file` override individual paths;
`--data-dir` isolates careers/Hall and instance ownership. The helper defaults
that directory to `working/player-data/`. `--recordings-dir` explicitly enables
recording. See [scripts](../scripts/README.md), [player data](playing.md#data-and-recovery)
and [asset import](components/assets.md).

`-SkipBuild` still checks source/executable identities and runs verification.
Omit it after changes. Each wrapper retains its own build log, results and receipt
beside its reports under `analysis/`; source identities include CMake files and
presets. The game wrapper installs only after passing, verifies the staged copy's
hash, and records the build/staged identities plus the artwork hash. A bare CMake
build does not create a wrapper receipt. Run one wrapper at a time per shared tree.

## Choose verification by change

Use `& $AmigaPython scripts/<command>` after loading `env.ps1`.

| Changed area | Command | Receipt directory |
| --- | --- | --- |
| Game flow, settings, launcher, persistence | `check_native_game.py` | `analysis/native-game/` |
| Same routes against original rules, screens and driving | `check_native_game.py --verify-original` | `analysis/native-game/` |
| Fresh track/race state and steering | `check_native_startup.py --verify-original` | `analysis/native-startup/` |
| Opening, raw keyboard/name editor, startup pixels | `check_native_opening.py` | `analysis/native-opening/` |
| Cockpit events, wheel state, first/repeated races, three-lap fixture | `check_native_cockpit.py` | `analysis/native-cockpit/` |
| Opponent AI, contact and painter | `check_native_opponents.py` | `analysis/native-opponents/` |
| Rendering, view isolation, both leagues/all tracks | `check_native_render_audit.py` | `analysis/render-audit/` |
| Recorded replay, interpolation, input clock, offline audio | `check_native_rendering.py` | `analysis/native-render/` |
| Physics driver and supporting stages | `check_physics_update.py` | `analysis/physics-update/` |
| Track decoding and initialization | `check_native_track_initialization.py` | `analysis/native-track-init/` |

Choose checks for the changed contract. Gameplay changes require relevant
original-code comparisons. Presentation changes require simulation/RNG isolation
checks and visual inspection; interactive devices/windows also need manual checks.
Documentation-only edits need link, command-reference and source-policy checks.
The rendering audit accepts `--jobs 1` if parallel MSBuild workers are unavailable.

CTest skips tests with missing private inputs. `ctest --preset windows-release -L unit`
runs the asset-free native subset. Configure-time `SCR_TEST_ASSET_PACK`,
`SCR_TEST_ADF`, `SCR_TEST_PAYLOAD` and `SCR_TEST_CAPTURE` select local fixtures.
`SCR_TEST_ALTERNATE_ASSET_PACK` adds cross-ADF career checks when a second pack exists.
Python policy/replay/lap/release tests are registered when an interpreter exists.
Window and host-storage checks run serially; rapid atomic-replacement tests can
still expose intermittent Windows access-denied errors, which remain failures.

CTest logs live under the build's `Testing/`; per-suite artifacts use
`test-output/<configuration>/`. Each suite resets only its own output. Direct
Python runs default to `build/windows/test-output/python/`; `SCR_TEST_OUTPUT_ROOT`
overrides that base. CTest does not import assets or refresh detailed wrapper receipts.

A retained pass applies only to its recorded sources, fixtures and executable.
Source moves invalidate earlier receipts. Inspect comparison contents as well as
`status`: a later ordinary game check can replace the combined report without
original comparisons. Game/startup wrappers also retain `original-check-results.json`.
Build success or a report merely existing does not establish comparison coverage.

## Original-code and replay prerequisites

Original-code tests use the matching payload at
`analysis/reference/vesuri/offline-extracted-payload.bin` and locally downloaded
Musashi under `analysis/reference/musashi/`. Verify its pinned files with:

```powershell
& $AmigaPython scripts/prepare_wheel_oracle.py
# Explicit download, only when pinned files are missing:
& $AmigaPython scripts/prepare_wheel_oracle.py --download
```

The lock is `setup/musashi-source-lock.json`; checks do not normally download it.
After preparing prerequisites, configure/build/test with `windows-oracle`.
[Research setup](research/reverse-engineering.md) covers payload mapping, external
references and captures. Cockpit/replay wrappers also need their retained capture
fixture and authored-control provenance; none is required to play the native game.

An interactive drive cannot be compared against an unrelated captured Practice
seed. Differential replay needs the exact race-entry state and controls.
Interactive recordings contain controls/traces but do not export that seed;
session tests export their own seeds. See [recordings](../tools/README.md#recordings-and-comparisons).

## Repository hygiene

```powershell
& $AmigaPython scripts/check_repository_assets.py --worktree
& $AmigaPython -m unittest discover -s tests/unit -p 'test_*.py'
git diff --check
```

The asset checker reads indexed blobs, including forced additions; `--worktree`
also checks non-ignored source. The hook must be enabled per checkout. CI runs
source policy and synthetic Python tests on Linux. Windows CI uses the Release
presets to build the player/tools, run asset-free CTest checks and validate the
staged release without local artwork. Original-code checks use local fixtures.
Original media, packs, saves, extracted data, built binaries and RE databases
stay local. Only the four pinned README screenshots may be tracked; the launcher
logo exception applies to the release EXE, not to tracking its PNG.
See [third-party notices](../THIRD_PARTY_NOTICES.md) for provenance and licence scope.

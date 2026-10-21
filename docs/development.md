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

## Prepared Windows environment

Commands below run from the project root in PowerShell. This checkout is at
`C:/Projects/Stunt Car Racer`.

| Requirement | Prepared location / version |
| --- | --- |
| C++17 compiler | Visual Studio 2026 C++ Build Tools; CMake generator `Visual Studio 18 2026`, x64 |
| CMake | 4.4.3 on PATH; project minimum 3.20 |
| Python | `C:/Tools/amiga-re/venv/Scripts/python.exe` |
| Python RE packages | Versions in `setup/requirements.txt` |
| Local disk | Supported ADF, identity in [asset guide](native-startup.md) |

The PowerShell launchers currently use the prepared Python path. On another
machine, configure equivalent tools and review `scripts/env.ps1` and the launcher
paths. Native Windows UI/audio/input use system libraries, including XInput;
the game executable has no Python or emulator runtime dependency. The supplied
launchers do use Python for import, build and checks.

A clone supplies source only. Copy your own supported ADF locally, or an already
imported matching pack. ROMs and captures are unnecessary for ordinary native
startup. Original-code oracles and capture-based replay need additional local
fixtures, described below.

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

For a build without the integration wrapper:

```powershell
cmake -S . -B analysis/native-game/build -G "Visual Studio 18 2026" -A x64 -DSCR_BUILD_ORACLE=OFF
cmake --build analysis/native-game/build --config Release --target native_game --parallel 4
& .\analysis\native-game\build\Release\native_game.exe `
  --asset-pack .\analysis\assets\stunt-car-racer.scr-assets
```

This only compiles and launches; it does not establish a passing test result or
refresh a wrapper's build receipt. `SCR_BUILD_ORACLE` defaults to OFF. Musashi
links only into separate verification targets when that option is enabled.

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
download it during a check. [Reverse-engineering setup](reverse-engineering.md)
describes the verified payload mapping, retained reference prerequisites and
capture extraction. The old capture-based wrappers are development tools, not
requirements for playing the native game. Cockpit/replay checks additionally use
the retained three-lap capture fixture and its authored control provenance.

Do not compare a fresh interactive drive against the default captured Practice
seed. A valid differential replay needs the exact race-entry state and controls
for that drive. Interactive recordings currently write controls and native traces,
without exporting that seed. Session tests explicitly export seeds for their own
recorded races; see [recordings](native-driving.md#recordings-and-verification).

## Repository hygiene and handoff

```powershell
& $AmigaPython scripts/check_repository_assets.py --worktree
& $AmigaPython -m unittest discover -s tests -p test_asset_policy.py
git diff --check
```

The asset check inspects indexed blobs and, with `--worktree`, non-ignored source.
CI runs only the source asset policy and its synthetic tests; it cannot run the
game without private inputs. Never force-add original media, packs, saves,
screenshots, extracted data, binary builds or RE databases. Keep derived files in
`analysis/` and working emulator/save/settings files in `working/`.

For a handoff, state what changed, which checks actually ran, any remaining
limitations and whether work is committed/pushed. Update the relevant guide when
its contract changes; keep the README and this entry path short. Do not turn
them into a running session log. Every Git commit carries:

```text
Co-authored-by: Codex <codex@openai.com>
```

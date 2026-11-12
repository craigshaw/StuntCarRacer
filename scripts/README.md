# Everyday development commands

Run commands from the project root. CMake presets and CTest are the normal build
and test interface; see [development](../docs/development.md).

| Command | Purpose |
| --- | --- |
| `env.ps1` | Load optional local tool configuration/discovery |
| `start-native-game.ps1` | Build/check and launch the player with development data |
| `start-native-race.ps1`, `start-native-replay.ps1` | Development viewer launch paths |
| `import_native_assets.py` | Import a supported ADF to the local asset pack |
| `check_native_*.py` | Focused native integration/original-code checks and receipts |
| `check_native_ghost.py` | Static-track ghost rendering proof, CTest checks and identified visual previews |
| `check_native_ghost_bridge.py` | Draw Bridge geometry matching, local fades, native league approach and identified previews |
| `check_native_laps.py` | Saved player ghosts, controls, feedback, storage and timing; `--verify-original` adds original timing/full-race comparisons |
| `check_native_timing.py` | Stage-1 crossing estimator and timestamp proof; `--configuration Debug` selects Debug, with source/fixture/executable receipts |
| `check_native_lap_timing.py` | Independent live timing and recovery accounting; Release/Debug CTest receipts and full-state isolation |
| `check_native_enhanced_store.py` | Separate Enhanced PB/Hall storage, Original regressions and process-interruption proofs; `--configuration Debug` selects Debug |
| `check_native_enhanced_sessions.py` | Dual completion delivery, category-specific PB/ghosts and pinned Enhanced challenges; Release/Debug receipts |
| `check_native_enhanced_ui.py` | Timing settings/migration, frame-aligned clocks, Hall filters and finish compatibility; Release/Debug receipts and PNG previews |
| `check_*physics*.py`, `check_motion_stages.py`, `check_road_response.py`, `check_transform_matrices.py`, `check_wheel_coordinates.py` | Recovered gameplay verification |
| `check_driving_replay.py`, `replay_lap.py` | Replay comparisons and authored-lap evidence checks |
| `prepare_wheel_oracle.py` | Validate or explicitly download pinned test-only Musashi sources |
| `check_repository_assets.py` | Source-only Git asset policy |
| `check_release.py` | Staged player file list, notices and embedded-resource safeguards |
| `native_build.py` | Shared build locations and source identities for wrappers |

Optional disk inspection, emulator preparation, REA/Ghidra checks and reference
analysis recipes live in [tools/research](../tools/research/README.md). Their
existing output and evidence paths under `analysis/` are unchanged.

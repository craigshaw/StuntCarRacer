# Retained evidence map

All paths below are relative to the repository root. The evidence stays at its
existing paths; this guide is an index, not a migration or cleanup recipe.

| Category | Existing location | Retention rule |
| --- | --- | --- |
| Supplied original media/artwork | Root ADFs, ROM and `logo.png` | Preserve original bytes; keep out of Git |
| Imported native assets | `analysis/assets/` | Derived pack and provenance identify the supported ADF |
| Captured emulator fixtures | `analysis/captures/`; original working snapshots in `working/` | Preserve sources, manifests, hashes and mapped RAM banks together |
| Recovered disk/RAM mapping | `analysis/map/`, `analysis/disk/` | Preserve offsets, hashes and mapping reports |
| Pinned external references | `analysis/reference/vesuri/`, `analysis/reference/musashi/` | Preserve provenance/lock identities; Musashi is test-only |
| Analysis exports/databases | `analysis/rea/`, `analysis/ghidra/` | Retain useful recovered evidence and source identities |
| Component verification | Existing component folders under `analysis/`, including `native-game/`, `native-startup/` and `physics-update/` | Keep reports, fixtures and identified historical executables at their recorded paths |
| Toolchain and cleanup records | `analysis/toolchain/`, `analysis/cleanup/` | Recorded diagnostics and retention receipts |
| Current disposable outputs | `build/windows/`, `build/windows-oracle/` | Compiler output, CTest logs and synthetic test artifacts can be recreated |

Use the [component index](../README.md) to find contracts and the
[development guide](../development.md#choose-verification-by-change) to choose
checks. A retained pass applies only to its recorded source, input and executable
identities. Source relocations require fresh verification receipts before reuse;
historical records remain historical evidence. Do not rewrite them to claim a
current pass. New test scratch data belongs under `build/`.

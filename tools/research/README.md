# Research utilities

Run from the project root after loading `scripts/env.ps1` when local tools are
needed. These utilities are optional and do not participate in the player build.

| Location | Purpose |
| --- | --- |
| `inspect_disk.py`, `build_initial_map.py` | Disk inspection and recovered disk/RAM map |
| `extract_winuae_state.py` | Extract retained emulator captures |
| `prepare_emulator.py`, `start-game.ps1` | Prepare local working copies and launch original WinUAE execution |
| `reference/` | Pinned disassembly/payload comparisons and extraction recipes |
| `ghidra/`, `verify-ghidra.ps1` | Independent 68000 Ghidra check |
| `verify-rea-mcp.mjs`, `verify-rea-wsl.mjs` | Explicit native/WSL REA transport checks |
| `local-tools.mjs` | Read optional local configuration for Node research checks |

`Play Stunt Car Racer.cmd` in this directory opens the original-game launcher.
Inputs and outputs retain their existing `analysis/` and `working/` locations.
See [research setup and mapping](../../docs/research/reverse-engineering.md),
[evidence locations](../../docs/research/evidence.md) and
[local configuration](../../setup/README.md).

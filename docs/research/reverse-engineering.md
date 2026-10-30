# Reverse-engineering tools and evidence

These tools support recovery and differential verification. The native game
uses the [local asset pack](../components/native-startup.md), without an emulator, ROM or saved
state. The [development guide](../development.md) covers normal builds and checks.

## Prepared tools

This is the installed environment verified for this checkout, not a claim about
the newest available releases.

| Tool | Version / location | Purpose |
| --- | --- | --- |
| REA | 6.7.0, global `rea` | Evidence-based binary analysis |
| Linux REA | Shared `rea_wsl` MCP; locally configured WSL distribution/account | Raw m68k analysis |
| Ghidra | 12.1.4; local `ghidra` setting and WSL installation | Disassembly/decompilation |
| Java | Local `javaHome`/`JAVA_HOME`; WSL OpenJDK 21 | Ghidra runtime |
| WinUAE | 6.0.3 x64; local `winUae` setting or PATH | Original execution and captures |
| Python | Local `python` setting, `.venv`, or PATH | Extraction and check wrappers |
| amitools / Capstone | 0.8.1 / 5.0.9 | Amiga formats / independent MC68000 decode |

Dot-source `scripts/env.ps1` for `$AmigaPython`, `$WinUae` and the `rea-wsl`
PowerShell helper. Python dependencies are pinned in `setup/requirements.txt`.
Tool paths and WSL choices belong in ignored `setup/local-tools.json`; see
[configuration](../../setup/README.md). Research utilities live in
`tools/research/`, with source recipes in `tools/research/reference/`.
The [evidence map](evidence.md) describes the retained data without moving it.
WSL provisioning/registration is recorded in `setup/install-rea-wsl.sh` and
`setup/register-rea-wsl.py`. Downloaded references and tool archives stay ignored.
Ghidra and WinUAE installer archives can be removed after installation. A fresh
WSL Ghidra installation needs the archive named and hash-pinned by its setup
script; an existing installation does not need that download retained.

Use **`rea_wsl` for raw 68000**. Its file arguments use Linux paths such as
`/mnt/c/Projects/Stunt Car Racer/analysis/...`. The native Windows REA server does
not admit m68k; do not disguise it as another CPU or file format. Extract
containers and justify load addresses before importing raw bytes. Ghidra's
`68000:BE:32:default` accepts a 68040 superset; cross-check questionable
instructions with Capstone MC68000 mode or WinUAE's 68000 execution.

REA's matching user skill is
`C:/Users/craig/.agents/skills/reverse-engineer-anything/SKILL.md`.
The shared Windows Codex configuration registers both native and WSL servers.
Existing app sessions may need reconnecting after configuration changes.

## Verified disk and memory mapping

The supported ADF is 901,120 bytes. SHA-256:
`548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
It has a valid boot checksum but no valid ordinary AmigaDOS root block; do not
repeat Hunk/ordinary-filesystem extraction discovery.

The plaintext payload is **408,540 bytes at disk offset `0xdc00`**, loaded at
**`0xe700`**, ending at `0x722dc` exclusive. SHA-256:
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Four separated RAM anchors in the WinUAE menu capture corroborate that mapping.
For runtime address `a`, payload offset is `a - 0xe700` and disk offset is
`a - 0xe700 + 0xdc00`. Boot-block listings use disk offsets unless stated otherwise.

The canonical retained payload is
`analysis/reference/vesuri/offline-extracted-payload.bin`. The disk/RAM map is in
`analysis/map/report.txt`, `manifest.json`, `regions.csv`, `code-symbols.csv` and
`data-symbols.csv`. The loaded RAM slice contains mutable state and converted
samples, so it is not interchangeable with the disk payload.

The pinned Vesuri reference revision is
`3cce78d193c3d0208be34d0bbfc3155e787cd406`. Its retained files and
`provenance.json` live under `analysis/reference/vesuri/`. It is an enhanced
disassembly: notably its timestep/scheduling differ from this disk. Labels aid
navigation; actual disk operands and observed execution take precedence.

With those pinned reference files/provenance already present:

```powershell
. .\scripts\env.ps1
& $AmigaPython tools/research/reference/extract_reference_payload.py
& $AmigaPython tools/research/reference/verify_selected_reference.py
& $AmigaPython tools/research/build_initial_map.py --capture analysis/captures/menu-user
```

These forensic scripts depend on retained local reference/capture inputs; a fresh
clone does not contain them or fetch them automatically. Restore/recreate the
appropriate evidence before running these commands. The ordinary asset importer
needs only the supported ADF and independently validates its payload hash.

Useful runtime anchors:

| Address | Recovered role |
| --- | --- |
| `0xed56` | Hardware/startup initialization |
| `0x1ca42` | Trigonometry tables, followed by other game data |
| `0x20184` | Cockpit palette and compressed image region |
| `0x5cf46` / `0x5baea` | Opening caller / main menu |
| `0x5d47e` | Main game loop |
| `0x61368` / `0x6185c` | Matrix producer / physics driver |
| `0x618ce..0x6194f` | Wheel-coordinate calculation |
| `0x6a594` / `0x72294` | Original framebuffers |

## Original game in WinUAE

Double-click `tools/research/Play Stunt Car Racer.cmd` or run `tools/research/start-game.ps1`.
The launcher prefers `working/stunt-car-racer-play.uae`: A500 PAL, 68000, OCS,
512 KiB chip plus 512 KiB slow RAM, normal disk speed, write-protected working
ADF, normal audio, USB Xbox controller on port 2 and mouse on port 1.

Click inside the window to capture input. Menus accept the displayed number and
Enter; the controller uses directions and A. Original driving keys are **S/D**
for steering, **Enter** for acceleration/boost and **Space** for brake/reverse
with boost. Joystick up/down operates gas/brake and A is fire/boost. Acceleration
is latched. **P/O** pause/resume, **F12** opens settings, **Alt+Tab** or the middle
mouse button releases input, and **Alt+F4** closes the emulator.

The controller profile is tied to its saved USB identity; another connection
may require selecting it again in WinUAE. `tools/research/start-game.ps1 -Settings`
opens settings before boot. Normal exit saves a unique
`working/captures/play-<timestamp>.uss`; a crash/forced kill is not a capture method.

`tools/research/prepare_emulator.py` prepares the working ADF and normalized Kickstart
from the supplied inputs. The source ROM is a mirrored 512 KiB image with two
identical 256 KiB halves; its SHA-256 is
`1d68ba18412501d2a4b307a0a632b94a50b839c2c7c5ff2df6de2c38b99a921f`.
The first half becomes `working/kickstart-1.3.rom`, SHA-256
`ee05862d8102a08436ac4056da7d549db31625c7d47b24dfb7b3c9a5c113ca53`.
The originals remain untouched. The generated `working/stunt-car-racer.uae`
is a quiet cycle-exact analysis profile; `-Analysis` selects it explicitly.
Its sound is disabled. A new checkout has no saved play/controller profile.

Extract a new snapshot into a new output directory:

```powershell
& $AmigaPython tools/research/extract_winuae_state.py working/captures/example.uss `
  --output analysis/captures/example
```

Original menu/race fixtures are retained under `analysis/captures/menu/`,
`menu-user/` and `race/`. Their manifests identify RAM, source snapshots and
registers. A snapshot is not an execution trace; stale buffers need not equal
freshly replayed output from the same visible state.

## Evidence and checks

The optional test-only MC68000 core is pinned in `setup/musashi-source-lock.json`.
Native and original sides evolve independently from the same identified input.
Checks declare projected RAM, write effects, call boundaries and hardware stubs;
read those seams before expanding a claim. See [verification by change](../development.md#choose-verification-by-change).

Low-level wrappers remain available: `check_wheel_coordinates.py`,
`check_transform_matrices.py`, `check_track_physics.py`, `check_road_response.py`,
`check_motion_stages.py`, `check_physics_update.py` and `check_driving_replay.py`.
Their matching `tools/research/reference/verify_*.py` scripts check source spans and
operands. Most need the retained payload, pinned reference metadata and captures.
Historical counts and complete reports remain in ignored `analysis/`.

REA dossiers and reusable immutable snapshots live under `analysis/rea/`;
component guides record relevant IDs, addresses and missing-body limitations.
Decompiler pseudocode is supporting evidence, not recovered source. Preserve
source hashes and offsets, distinguish static inference from execution, and
keep media, dumps, generated code/binaries and analysis databases out of Git.

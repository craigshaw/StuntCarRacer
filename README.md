# Stunt Car Racer — Amiga reverse engineering

Setup verified on 18 October 2026. The supplied ADF reaches the game’s main menu
in WinUAE using the supplied Kickstart 1.3 ROM and the prepared A500 PAL config.
The Linux REA bridge is now installed and verified for raw 68000 analysis through
both CLI and the Windows-to-WSL MCP transport.

## Installed tools

| Tool | Version / location | Purpose |
| --- | --- | --- |
| REA | 6.7.0; global `rea` command | Agent MCP tools and evidence-based binary analysis |
| Ghidra | 12.1.4; `C:\Tools\ghidra_12.1.4_PUBLIC` | Native disassembly, decompilation, scripting and references |
| Java JDK | Existing 23; `C:\Program Files\Java\jdk-23` | Verified compatible with this Ghidra/REA installation |
| WinUAE | 6.0.3 x64; `C:\Tools\WinUAE-6.0.3\winuae64.exe` | Amiga execution and debugging |
| amitools | 0.8.1; `C:\Tools\amiga-re\venv` | `xdftool`, `hunktool`, `romtool` and Amiga formats |
| Capstone | 5.0.9 in the same Python environment | Independent MC68000 disassembly |
| WSL / Ubuntu | WSL 2, Ubuntu-24.04; default user `craig` | Linux analysis host |
| Linux REA / Node | REA 6.7.0, Node 24.15.0 under `/home/craig/.local` | Raw m68k CLI and MCP analysis |
| Linux Ghidra / Java | Ghidra 12.1.4 under `/home/craig/.local/opt`; OpenJDK 21.0.12.1 | Linux native decompiler |

Node 24.15.0 and Python 3.12.6 were already installed. Existing Ghidra 11.1.2
remains at `C:\Tools\Ghidra 11.1.2`. No paid decompiler is required.

## REA across Codex desktop and CLI

REA's own setup installed the `rea` stdio MCP server into
`C:\Users\craig\.codex\config.toml` and its version-matched skill into
`C:\Users\craig\.agents\skills\reverse-engineer-anything`. These are shared
user locations for local desktop and CLI Codex sessions using this Windows user
and default Codex home. Remote/cloud hosts and another `CODEX_HOME` need their own
installation. The server uses absolute Node/REA paths and explicitly supplies
the Ghidra and Java locations, so it does not depend on a terminal's PATH.

The original config backup is `C:\Users\craig\.codex\config.toml.rea.backup`.
The backup immediately before adding WSL is `config.toml.rea-wsl.backup` in the
same directory. The user's `GHIDRA_INSTALL_DIR` environment variable is also set.
Unrelated Codex configuration was preserved.

The shared Windows config now also registers **`rea_wsl`**, launching
`wsl.exe --distribution Ubuntu-24.04 --user craig --exec /home/craig/.local/bin/rea-wsl mcp`.
Use this server for m68k. Windows paths must be supplied in Linux form, e.g.
`/mnt/c/Projects/Stunt Car Racer/analysis/toolchain/return-five.bin`.
Its startup timeout is 60 seconds and tool timeout is 450 seconds.

REA setup also created `/home/craig/.codex/config.toml` and installed its matching
skill under `/home/craig/.agents/skills` for Codex sessions run inside Ubuntu.
This prepares the integration; it does not install the Codex CLI in Ubuntu.

**Restart/reconnect existing Codex desktop and CLI sessions to load the MCP
server.** The skill is available on the next turn; restart if it does not appear.
The WSL setup session used REA through its CLI and a standalone MCP client;
that proves the server works, not that an already-running Codex session reloaded it.

Verification passed:

- REA setup reported `ready`; Codex registration and bundled skill are aligned.
- REA's Ghidra readiness checks passed, including Windows native process controls.
- A real stdio MCP handshake advertised REA 6.7.0 and 207 tools.
- MCP `open_binary`, `analyze_function`, and `close_binary` succeeded through
  Ghidra using a generated 6502 fixture supported by the Windows bridge. The
  returned assembly and pseudocode both express a return value of five.
- A separate native Ghidra test disassembled and decompiled a generated **68000**
  function to `return 5;`. This is the test relevant to Amiga CPU support.
- The **Linux REA CLI and Windows-to-WSL MCP transport** both analyzed that
  68000 fixture at `0x1000`, returning `moveq 0x5, D0`, `rts`, and `return 5;`.
  REA's independent load-image verification passed all 23 checks, including
  source SHA-256, language, entry point, mapped extent and byte identity.

### Choosing the correct REA connection

REA 6.7.0 admits raw m68k images on its Unix Ghidra path, but its Windows bridge
admits only supported native PE targets and raw NMOS 6502 images. Windows rejects
m68k. Our Linux/WSL test now passes, so select **`rea_wsl`** for this game's
extracted 68000 code. The separate Windows **`rea`** connection remains available
for native Windows targets. Neither ADF nor Amiga Hunk is handled by REA's raw
loader: code must first be extracted with a justified load address. The passing
fixture test verifies the toolchain; it does not establish a game code layout.

## Use the prepared tools

From the project root in PowerShell:

```powershell
. .\scripts\env.ps1
rea --version
rea doctor --client codex --provider ghidra --skill --json
& $AmigaPython .\scripts\inspect_disk.py
node .\scripts\verify-rea-mcp.mjs
node .\scripts\verify-rea-mcp.mjs --native
node .\scripts\verify-rea-wsl.mjs
.\scripts\verify-ghidra.ps1
.\scripts\start-game.ps1
```

For 68000 analysis from PowerShell after sourcing `env.ps1`:

```powershell
rea-wsl function '/mnt/c/Projects/Stunt Car Racer/analysis/toolchain/return-five.bin' 0x1000 --target-format raw-binary --architecture m68k --entry-point 0x1000 --provider ghidra --json
```

The address in this example belongs to a synthetic setup fixture. Supply the
observed load/entry address for a real extracted game region.

Ghidra's first launch writes user preferences, and analysis tools create temporary
working directories; Codex may require its normal filesystem approval for these.
Ghidra GUI launcher: `C:\Tools\ghidra_12.1.4_PUBLIC\ghidraRun.bat`.
The headless launcher is under that installation's `support` directory.

The emulator config is generated by `scripts/prepare_emulator.py` under
`working/stunt-car-racer.uae`: PAL A500, 68000, OCS, 512 KB chip RAM plus 512 KB
slow RAM, cycle-exact settings, no JIT, normal disk speed, and write-protected DF0.
Sound is disabled in this analysis profile. F12 opens WinUAE settings.

### Playing and capturing with the USB Xbox One controller

Double-click `Play Stunt Car Racer.cmd`, or run `scripts/start-game.ps1`.
The launcher uses the saved `working/stunt-car-racer-play.uae` when present:
Xbox One controller on Amiga port 2, mouse on port 1, automatic port switching
disabled, normal audio, and the original Amiga keyboard available alongside it.
Craig confirmed both controller and keyboard work, then reached the track in
Practice on 18 October 2026. The controller's saved identity is specific to this
USB connection; reconnecting through Bluetooth may require selecting it again.

| Action | Xbox controller | Original keyboard |
| --- | --- | --- |
| Menu navigation | D-pad/stick up and down | Number of menu entry |
| Select / continue | A | Enter |
| Steer | D-pad/stick left and right | S / D |
| Accelerate | Push up, then release | Enter (also uses boost while held) |
| Brake / reverse | Pull down | Space (also uses boost while held) |
| Boost | A | Included in Enter / Space |

These game controls follow the [original operating instructions](https://www.lemonamiga.com/doc/stunt-car-racer/1570).
Keyboard and joystick acceleration are latched: releasing the control does not
automatically stop acceleration. Click inside WinUAE to focus/capture input;
Alt+Tab or the middle mouse button releases it. F12 opens emulator settings.
The game uses P to pause and O to resume. Avoid pressing Escape during automated
computer use, since it also stops the automation helper.

Normal exit (Alt+F4 or the window's close button) saves a unique
`working/captures/play-YYYYMMDD-HHMMSS-mmm.uss`. A crash or forced process kill
does not provide that guarantee. Use `scripts/start-game.ps1 -Settings` to open
settings before boot, or `-Analysis` to explicitly choose the quiet analysis
profile. The source ADF and ROM remain untouched.

## Inputs and first observations

Original ADF: `Stunt Car Racer (MicroStyle).adf`, 901,120 bytes.

SHA-256: `548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.

It has a `DOS\0` boot signature and valid boot-block checksum. The declared root
block is 880, but it is not a valid AmigaDOS root; `xdftool -r ... list` reports
`Invalid Root Block`. The boot block contains a Rob Northen Computing 1989
protection notice at disk offset 130. These observations suggest a custom disk
loader/layout; they do not establish the later game's packing or encryption.
Booting to the main menu and reaching the track in Practice are verified;
complete races and any later protection checks are not yet tested.

The supplied ROM has revision 34.5 (Kickstart 1.3) and two identical 256 KB halves.
`prepare_emulator.py` extracts one half into `working/kickstart-1.3.rom`, which
passes `romtool` header, footer, size, checksum, reset and Kickstart validation.
The original 512 KB ROM is preserved. Original SHA-256:
`1d68ba18412501d2a4b307a0a632b94a50b839c2c7c5ff2df6de2c38b99a921f`.
Normalized SHA-256:
`ee05862d8102a08436ac4056da7d549db31625c7d47b24dfb7b3c9a5c113ca53`.

Evidence is under `analysis/disk` and `analysis/toolchain`. The boot listing is
linear MC68000 disassembly with file offsets, not observed runtime addresses.
Ghidra's `68000:BE:32:default` language is a 68040 superset; check questionable
instructions against the MC68000 decoder or real emulated execution.

## Loaded game and initial map (2026-10-18)

Start with [the map report](analysis/map/report.txt),
[region map](analysis/map/regions.csv), and
[verification manifest](analysis/map/manifest.json).
The intended outcome is a readable native source port. WinUAE remains the
external reference and capture tool, not the intended application's runtime host.

Recovered the main-menu state with WinUAE 6.0.3's `statefile_quit` option in a
separate instance, preserving the existing game window. The snapshot is
`working/captures/menu-auto.uss`; extracted RAM, CPU registers, configuration and
chunk hashes are under `analysis/captures/menu/`. It contains 512 KiB chip RAM
at `0x000000` and 512 KiB slow RAM at `0xc00000`. The captured PC is `0x612c8`,
inside the keyboard-state query at `0x612be`. This is not a racing execution trace.
The saved configuration records `sound_output=interrupts`; it produces no host
audio and is not an audio-fidelity baseline.

The independently user-saved `working/captures/menu.uss` was also checked.
Its RAM and registers are under `analysis/captures/menu-user/`; `comparison.json`
records successful checks of the four mapping anchors, three selected routines,
and all converted sample bytes. Its captured PC is also `0x612c8`.

The disk uses the reference loader's plaintext-layout branch. Its 408,540-byte
payload begins at ADF offset `0xdc00`. Four separated unique RAM matches
corroborate loading at `0xe700`, ending at `0x722dc` (exclusive).
Payload SHA-256: `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
The exact captured slice is `analysis/map/loaded-game-e700.bin`, with its own
hash in the manifest. Do not substitute one for the other: 332,919 bytes match
(81.49%), while 75,621 differ. All 46,482 source sample bytes match the startup
XOR-`0x80` conversion. Other differences include generated samples, mutable
state and framebuffer reuse; individual remaining changes are not all explained.

Reference: [Vesuri/stuntcarracer at the pinned revision](https://github.com/Vesuri/stuntcarracer/tree/3cce78d193c3d0208be34d0bbfc3155e787cd406).
Source files, hashes and comparison evidence are under `analysis/reference/vesuri/`.
This is an enhanced disassembly. For example, the original instruction at
`0x61956` uses timestep value `0xee` (238); the reference uses 39 for PAL.
Its names are useful, but its changes must not silently enter the faithful port.

| Runtime address | Initial identification | Evidence |
| --- | --- | --- |
| `0xed56` | Hardware and interrupt initialization | Original instructions; saved vectors agree |
| `0x1ca42` onward | Maths tables, then names/track data | Source labels and unchanged RAM; meanings provisional |
| `0x20184` onward | Graphics assets | Source labels and unchanged RAM; formats still to parse |
| `0x5d47e` | Main game loop | Nine original calls correspond to reference names |
| `0x6185c` | Physics driver | 114 unchanged bytes; 17 call targets identified |
| `0x618ce–0x6194f` | Wheel-coordinate calculation | All 29 instructions and operands match; 130 bytes |
| `0x6a594`, `0x72294` | Framebuffers | Startup pointers and 32,000-byte spans |

`code-symbols.csv` has 160 constrained prefix matches plus the main-loop label.
These are navigation candidates, not 161 proven original functions.
`data-symbols.csv` has 897 source equates; some enhanced-source names describe
reused fields. Keep their meanings provisional. `regions.csv` identifies mixed
regions and uncertain boundaries; it is not a fully classified disassembly.
`changed-ranges.csv` records every disk-versus-RAM difference.

REA/Ghidra independently verified both raw imports with 23 mapping/hash checks
each. Its wheel-coordinate assembly and pseudocode agree between disk and RAM,
and the assembly agrees with Capstone's MC68000 decoder. Evidence bundles and
reusable immutable snapshots are under `analysis/rea/`; opened sessions were closed.
Pseudocode is decompiler output, not recovered original source.

To reproduce using the retained pinned reference files:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/reference/extract_reference_payload.py
& $AmigaPython scripts/reference/verify_selected_reference.py
# Use a NEW output directory if extracting another snapshot; captures are preserved.
& $AmigaPython scripts/extract_winuae_state.py working/captures/menu-auto.uss --output analysis/captures/menu-recheck
& $AmigaPython scripts/build_initial_map.py --capture analysis/captures/menu-recheck
```

The selected verifier also rebuilds the prefix-match inventory. Scripts under
`scripts/reference/` are our analysis tools; downloaded upstream source was not
executed. All game bytes, dumps and reference copies remain excluded by `.gitignore`.

### First readable native function and race verification

`src/wheel_coordinates.cpp` implements `scr::calculateWheelCoordinates` as an
ordinary C++17 function over six input words, returning three coordinate pairs.
There is no CPU dispatcher or emulator dependency in the `scr_physics` library.
Point names follow the matched reference; the recovered producer below uses
signed Q15 coefficients. Physical coordinate conventions remain provisional.
The implementation explicitly preserves the 68000's downward
rounding for signed shifts and its operation order. Arithmetic bounds show that
this particular routine cannot overflow its word intermediates for any inputs.
The detailed contract is in `analysis/wheel/arithmetic.txt`.

Race capture: `working/captures/play-20261018-192934-740.uss`, saved on normal
exit after Craig confirmed that the car had landed in Practice. SHA-256:
`5ac753f0d452fe628b627d45817a0c27105e2bec12f2640f1518a73adfb58c7f`.
Extracted RAM, CPU state and configuration are in `analysis/captures/race/`.
The wheel routine's 130 bytes are identical to the original disk payload.

`tests/wheel_oracle.cpp` executes those actual original bytes in Musashi's
MC68000 core and compares the six output words with the native function.
Musashi is pinned and belongs exclusively to this test executable. The latest
run passed **652,480 cases with zero mismatches**: all 65,536 bit patterns in each
input independently, 9,261 boundary combinations, 250,000 deterministic random
combinations, and three captured input sets. Each execution also checks the
29-instruction path, return stack and writes confined to the six output words.
These tests are not an exhaustive enumeration of all 2^96 input combinations.

For the race inputs, all three agree: freshly executed original code, native
C++, and already-stored WinUAE snapshot outputs. The signed coordinate pairs
are front-left `(-513, 1023)`, front-right `(509, 1023)`, rear-center `(2, -1023)`.
The menu snapshots contain stale zero output words; their captured inputs still
produce matching original/native results when replayed. A saved state is not a
trace, and this function contract does not claim equivalent CPU flags, register
side effects, interrupt timing or full-game behavior.

Reproduce on this machine (CMake and Visual Studio 2026 Build Tools are already
installed):

```powershell
. .\scripts\env.ps1
# Verifies the 17 pinned source files; downloads missing files only when requested.
& $AmigaPython scripts/prepare_wheel_oracle.py --download
# Builds and checks the fixture, captures, source hashes and original/native outputs.
& $AmigaPython scripts/check_wheel_coordinates.py
```

The source lock is `setup/musashi-source-lock.json`. The test command re-extracts
the 130-byte fixture from the hash-verified payload, verifies the saved-state and
RAM hashes, rebuilds, and writes provenance and results to
`analysis/wheel/check-results.json`; build output is in `analysis/wheel/build.log`.
To include a new extracted snapshot, pass repeatable `--capture <directory>`
options. Generated code, binaries, dumps and upstream source remain local and
ignored. Native library builds can leave `SCR_BUILD_ORACLE` at its default OFF.

### Readable angle-to-matrix calculation and chained verification

`src/transform_matrices.cpp` now implements `scr::updateTransformMatrices`,
`scr::gameSine` and `scr::gameCosine` as ordinary native C++17. The matrix producer
reads the three car angle words and the track heading, including the wrapped
difference between yaw and track heading. A full turn is 65,536 angle units.
Pitch/yaw/roll are provisional names for the reference's X/Y/Z fields; the
physical axes still need to be established in the wider physics model.

| Recovered item | Original runtime range | Verified against pinned reference |
| --- | --- | --- |
| Matrix producer | `0x61368–0x6158b` | All 142 instructions and resolved operands |
| Sine/cosine helpers | `0x64d08–0x64d65` | All 32 instructions and resolved operands |
| Quarter-wave table | `0x1ca42–0x1cc45` | All 258 words identical in disk payload and race RAM |

REA/Ghidra evidence and snapshots are in `analysis/rea/transform-matrices-*`;
the independent MC68000/reference comparison is
`analysis/transform/reference-comparison.json`. The original sine/cosine table
is extracted locally and supplied through `scr::TrigTable`; no game table bytes
are embedded in the tracked C++ source. The helpers preserve the game's table
interpolation and quadrant rules. Matrix products preserve each Q15 rounding
step and 16-bit wraparound, rather than substituting floating-point trigonometry.

The producer updates 30 words in a 36-word coefficient block and leaves six
slots untouched. The C++ preserves those slots and exposes the six wheel inputs
through `TransformMatrices::wheelInputs()`. The reference's historical names
`matrix44`, `matrix46` and `matrix48` actually correspond to byte offsets
`0x42`, `0x44` and `0x46` from the block base, respectively.

The native chain is:

```cpp
scr::TransformMatrices matrices{};
scr::updateTransformMatrices(angles, originalTrigTable, matrices);
auto wheels = scr::calculateWheelCoordinates(matrices.wheelInputs());
```

`tests/transform_oracle.cpp` executes the actual original producer and trig
bytes in the pinned test-only MC68000 core. Verification passed with **zero
mismatches**:

- **131,072 trig cases:** every 16-bit angle through both sine and cosine.
- **381,350 matrix cases:** 262,144 independent angle sweeps, 19,200 quadrant
  and table-boundary combinations, 100,000 deterministic random combinations,
  and six replays from the three captures using captured/randomized old buffers.
- **381,350 chained wheel cases:** original matrix output feeds the original
  wheel routine; independently produced native matrices feed the native routine.
- All 36 matrix words are compared. Execution checks constrain reads, writes,
  call targets, stack traffic and return behavior; unrelated registers and
  condition flags are randomized. The original wheel suite still passes all
  **652,480 cases** after adding this implementation.

The race's current angles replay to slightly different coefficients than those
already stored in the snapshot: for example, two wheel inputs become `-38`
instead of `-41`. The physics driver calculates matrices before later angle
updates, which is consistent with this difference; the snapshot alone does not
establish the exact update history. Both implementations receive identical
angles in the comparison. Their resulting race wheel coordinates also agree
with the snapshot's stored coordinates. Stored-state agreement is reported
separately from original/native agreement.

Reproduce using the retained inputs and pinned Musashi sources:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/reference/verify_transform_reference.py
& $AmigaPython scripts/check_transform_matrices.py
& $AmigaPython scripts/check_wheel_coordinates.py
```

The transform wrapper verifies payload, code/table spans, captures and source
hashes before building. Results and provenance are in
`analysis/transform/check-results.json`; build output is in
`analysis/transform/build.log`. Repeatable `--capture <directory>` options select
other extracted captures. Code, table fixtures, RAM and executables stay ignored.

The trig input domain is exhaustive for the original table; the matrix tests
are not all `2^64` angle combinations. The native contract covers final
coefficient memory and wheel outputs, not original registers, CPU flags or
timing. Captured-input replay is not an instruction trace or proof of full-game
fidelity. The native library still has no emulator dependency.

### Road interpolation, expected wheel heights and spring forces

Three further stages are now readable C++ with comparisons against original
68000 execution. They extend the `scr_physics` library and have no emulator
dependency. These arithmetic stages now feed the track-height selector and
suspension response described in the next section.

| Native function | Original runtime range | Purpose | Passing differential cases |
| --- | --- | --- | --- |
| `interpolateTrackCoordinate` | `0x5c554–0x5c5f1` | Blend four track control values using lateral and longitudinal fractions | 1,393,658 |
| `calculateExpectedWheelHeights` | `0x61b70–0x61bcb` | Derive the body's three expected wheel heights from height, pitch and roll | 497,269 |
| `calculateSuspensionSpringForces` | `0x622dc–0x62423` | Convert suspension travel and average wheel velocity into spring components and angles | 755,206 |

Implementations and contracts are in `src/track_interpolation.*`,
`src/expected_wheel_heights.*`, and `src/suspension_springs.*`. All **2,646,133**
comparisons passed with zero mismatches, including inputs from both menu
captures and the race capture. The earlier wheel and matrix suites also pass.
The five newly recovered original routines, including two spring helpers,
match all **157 instructions and resolved operands** of the pinned reference.

The track interpolation preserves wrapped 16-bit endpoint differences, the
large-delta scaling branch, and the negative-product low-byte truncation.
A generic floating-point bilinear interpolation would change results. Tests
cover every pair of 8-bit fractions for 12 representative sets of control
values, complete independent control-word sweeps, exact branch boundaries and
random combinations. All four arithmetic paths executed. The function is
named for coordinates because the original helper can be reused beyond heights.

Expected wheel heights use the already verified original sine table. The rear
height subtracts the pitch offset; the two front heights add it and apply
opposite roll offsets. Each intermediate retains the original 32-bit wrap,
followed by a signed right shift of eight bits. Tests compare the three output
longwords and the shared pitch-sine scratch word, including overflow and
negative rounding cases. These body heights are later compared with sampled
road heights; they are not themselves road samples.

The spring stage includes `attenuateDisplacementValue` (`0x62424–0x62453`)
and the helper labelled `multiplyAndRandomize` (`0x612a2–0x612bd`). Despite that
name, the latter is a deterministic byte multiply: the high product byte is
returned and the low byte is retained in scratch memory. Its C++ name reflects
the actual operation. Attenuation uses the locally extracted 128-byte table
at `0x1eeca–0x1ef49`, with no table bytes embedded in tracked source.
The port reproduces all six scratch bytes and retains five untouched bytes in
the output block. Tests compare all 26 bytes, sweep both derived displacement words
exhaustively, and cover travel/velocity boundaries and random combinations.

The original attenuation helper implicitly requires bits 8–15 of register D1
to be zero. The preceding view-angle calculation and intervening caller code
establish this for the normal physics path; the native spring contract covers
that path. **1,280 separate interpreter probes** confirm what happens with
every possible D1 high byte. They are evidence for the precondition, not native
differential cases. In particular, the original `-32768` displacement wraps on
negation and selects a zero magnitude: the port preserves that behavior.

Run the complete new verification on this machine:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_track_physics.py
```

This validates the pinned ADF/payload, reference source, original code/table
spans, capture hashes and Musashi sources, then builds and runs all three
oracles. Results, source/executable identities and limitations are recorded in
`analysis/track-physics/check-results.json`; compiler output is in `build.log`
beside it. Repeatable `--capture <directory>` selects other extracted captures.
Detailed original listings, source comparisons and caller evidence are under
`analysis/track-interpolation/`, `analysis/expected-heights/` and
`analysis/suspension-springs/`. REA evidence and a reusable snapshot are in
`analysis/rea/track-physics-*`, indexed by `analysis/rea/index.json`.

The oracles check original code paths, permitted memory accesses, stack/return
behavior and final native outputs. Stored capture values are reported separately:
they can be stale, shared scratch can have been reused, and current inputs need
not belong to the same update. These tests do not exhaust every combined input,
establish physical units, or verify full-loop timing and gameplay fidelity.

### Track sampling, wheel contact and suspension response

The surrounding road and suspension calculations are now readable native C++17.
The implementation preserves the original disk's fixed-point arithmetic,
including word/byte wraparound and branches that depend on instruction flags.

| Native function | Original runtime span(s), end exclusive | Passing differential cases |
| --- | --- | --- |
| `applyWheelHeightContact` | `0x5c5f8–0x5c65a`, `0x5c808–0x5c890` | 672,663 |
| `sampleTrackWheelHeights` | Selector `0x5c1d0–0x5c3da`, plus five configuration/point/transition helpers | 251,353 |
| `updateSuspensionResponse` | Prefix `0x61bcc–0x62042`, momentum helper `0x6180e–0x6181e`, and the recovered spring helpers | 618,379 |
| `updateRoadSuspensionStep` | Composition of five recovered entrypoints | 1,281 |

All **1,543,676 comparisons passed with zero mismatches**. Implementations and
input/state contracts are in `src/wheel_contact.*`, `src/track_sampling.*`,
`src/suspension_response.*` and `src/road_suspension_step.*`.

Track sampling now selects segments, loads their geometry configuration, moves
across adjacent segment boundaries and samples the rear, right and left wheels
in the original order. It calls the recovered interpolation and contact code.
Its geometry and generated segment arrays are explicit inputs, extracted from
RAM or constructed by the test fixtures; they are not all immutable disk data.
The race fixture has 44 segments. Tests cover every segment, both geometry
formats, descriptor/boundary combinations, complete origin-word sweeps and
100,000 random cases. The two menu snapshots have no loaded track and are
explicitly skipped by the complete sampler and composed-step tests.

Wheel contact includes the edge drop, bounce phase and rendering-order flags.
At low speed and small pitch, it smooths height using the original unsigned
33-bit sum before halving. The lateral sign used for bounce comes from the
high byte of the big-endian position word. Tests cover all 16-bit lateral
positions, every speed/pitch byte pair, boundary flags and arbitrary 32-bit
heights, as well as interpolation followed by contact handling.

Suspension response compares sampled road heights with expected body heights,
clamps travel, derives compression velocity and applies the original momentum
factor. It updates impact/damage accumulation counters, derives target roll
and pitch rates, applies the settling rules and calls the spring-force stage.
It preserves details such as clearing only the high byte of the maximum
compression word and updating one shared compression counter sequentially for
all three wheels. The native boundary is immediately before the countdown call
at `0x62042`; later opponent collision and impact-audio work is still unported.

Comparison with the pinned enhanced disassembly found differences that matter
to fidelity:

- The original sampler reads its lateral origin from `0x1bc5e`; the enhanced
  source names `0x1bc5c`. These words differ in the race capture. The native
  input is named `lateralOrigin` without assuming the enhanced label is right.
- The original lateral clamp reads a byte from RAM address `0xff`; the enhanced
  source substitutes an immediate `0xff`. The native contract supplies that
  original RAM byte explicitly.
- The original momentum helper uses factor `0x114` and has a zero-result branch
  omitted by the enhanced source. Both original behaviors are retained.

The sampler's six routines contain 269 verified instructions across 1,190 bytes;
the comparison resolves every operand and records the two sampler differences
explicitly. The suspension prefix contains 235 instructions, with the momentum
helper's differences recorded separately. REA/Ghidra was used to inspect these
routines, with MC68000 disassembly and execution as cross-checks. In particular,
the suspension branch after the momentum call depends on the final word-add
flags; the decompiler's simpler-looking condition is not equivalent.

`scr::updateRoadSuspensionStep` connects the recovered native stages:

```text
angles -> matrices -> wheel coordinates -> sampled road heights --+
angles + world height -> expected body heights ------------------+-> suspension response
                                                                     and spring forces
```

Its oracle executes the same five original entrypoints with identical external
inputs, then compares all 512 KiB of RAM except the test's vectors, sentinel and
stack. It checks the shared scratch aliases in the original write order. The
1,281 cases comprise the race inputs, 256 sequential state updates and 1,024
randomized replays. Individual stage tests additionally cover much larger
input sweeps. Musashi is confined to the tests; the native library has no CPU
dispatcher or emulator dependency.

This is a composition of selected stages, **not a complete physics tick**. The
original intervening world-coordinate transform, wheel-speed update and
view-angle calculation remain to be recovered. Inputs normally produced by
those stages are supplied explicitly, and the original D1/D2 register
preconditions are established at test entry. The composed native state keeps
named results for each stage even where the original reuses a scratch address.
Tests verify matching computation from matching inputs, not that an arbitrary
snapshot's stored outputs belong to its current inputs.

Reproduce with the retained payload, captures and pinned test sources:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_road_response.py
```

The wrapper validates source, payload, original code/table spans, capture and
Musashi hashes, rebuilds, and runs all four oracles. Results and provenance are
in `analysis/road-response/check-results.json`, with compiler output in
`analysis/road-response/build.log`. Detailed listings, source comparisons and
arithmetic contracts are under `analysis/wheel-contact/`,
`analysis/track-sampling/` and `analysis/suspension-response/`. REA evidence and
a reusable snapshot are under `analysis/rea/road-suspension-*`, indexed in
`analysis/rea/index.json`. Repeatable `--capture <directory>` arguments select
other extracted snapshots. The earlier wheel, matrix, interpolation, expected
height and spring-force regression suites also pass.

Next: recover the intervening world transform, wheel-speed and view-angle
stages, then the remaining motion/angle integration and suspension tail. This
will let us compare a complete physics update and, eventually, recorded input
sequences. Full gameplay, audio fidelity, asset formats, physical units and
complete function boundaries remain outstanding; these tests do not establish
full-game timing or exhaust every combined input.

## Sources and installation provenance

- [REA documentation](https://github.com/morluto/rea/blob/main/docs/installation.md)
  and [raw binary/m68k limits](https://github.com/morluto/rea/blob/main/docs/raw-binary.md).
- [Codex user skill locations](https://learn.chatgpt.com/docs/build-skills).
- [Ghidra 12.1.4 release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build).
  Its downloaded archive matched GitHub's published SHA-256:
  `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
- [WinUAE official download page](https://www.winuae.net/download/) linked the
  6.0.3 x64 archive on download.abime.net. The executable's Authenticode signature
  was valid. Recorded archive SHA-256:
  `f8b7c44e8ab2f68db49b4205793848b4496464dbee3c036921840d82627f4fc3`.
- [amitools](https://github.com/cnvogelg/amitools) and
  [Microsoft WSL installation](https://learn.microsoft.com/en-us/windows/wsl/install).

Downloaded upstream docs and archives are retained under `setup/upstream` and
excluded from source control. Python package versions are in
`setup/requirements.txt`. Linux provisioning is recorded in
`setup/install-rea-wsl.sh`; Windows bridge registration is in
`setup/register-rea-wsl.py`. The Node archive was checked against Node's published
SHA-256 list, and the Linux Ghidra uses the same verified distribution archive.
Use `rea update` for a deliberate future update, then
review `rea setup --client codex --dry-run --json` before refreshing registration
and skill. Recheck the m68k/Windows support boundary when updating.

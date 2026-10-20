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
view-angle calculation are omitted by this earlier composition. They are now
recovered and connected in the next section. The earlier test supplies their
external inputs explicitly and establishes the original D1/D2 register
preconditions at each entry. The composed native state keeps
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

### Motion transforms, wheel speed, damping and integration

The next stages are implemented in `src/motion_transforms.*`,
`src/wheel_motion.*` and `src/motion_integration.*`. They are ordinary native
functions over named inputs and state, using the original integer arithmetic.

| Recovered stage | Original runtime range, end exclusive | Native behavior |
| --- | --- | --- |
| World-coordinate transform | `0x6158c–0x615e6` | Converts world speed to local X/Z velocity; retains local Y |
| View-angle terms | `0x615e6–0x61618` | Scales selected matrix coefficients by -317 or +317 |
| Secondary coordinates | `0x61618–0x61672` | Produces three world-acceleration words from the adjusted inputs |
| Tertiary coordinates | `0x61672–0x616d6` | Transforms angular velocity, including the dependent Z calculation |
| Wheel speed | `0x60fbe–0x61012` | Updates speed magnitude and wheel rotation speed |
| Velocity damping | `0x62138–0x6217a` | Produces angular acceleration X/Z, retaining Y |
| Angular-velocity integration | `0x61b26–0x61b70` | Adds the scaled angular acceleration to rotation velocity |
| World-velocity integration | `0x61adc–0x61b26` | Adds the scaled world acceleration to world velocity |
| Position/angle integration | `0x61950–0x61ad4` | Advances position and angles, applies limits and updates pitch/view state |

The shared matrix helpers occupy `0x61338–0x6135a`. Each signed product is
rounded and truncated before summing; rounding a complete dot product would
change results. The reference's `getNegativeTrigValue` and
`getPositiveTrigValue` names describe coefficient scaling here, not fresh
sine/cosine lookups. Axis names and physical units remain provisional.
The 22-byte selector table at `0x1ec46` and four angle-limit words at `0x61ad4`
are supplied from the verified local payload, not embedded in tracked source.

Further original/enhanced-source differences are preserved explicitly:

- Original wheel speed lives at `0x1bc62`; the enhanced source names
  `0x1bc60`. The original decay subtracts one quarter, using a logical shift
  by two; the enhanced source shifts by five.
- Original angular damping always shifts rotation velocity by four. Wheel
  contact controls only the additional yaw contribution. The enhanced source
  adds separate ground-damping branches with a shift by one.
- All twelve integration timestep writes use the original factor **238/256**.
  The enhanced reference uses 39/256 for PAL and 34/256 for NTSC. Three original
  branches removed from that source are also accounted for in the comparison.

Position integration preserves 32-bit wrapping, the fractional low word when
clamping world height, and the asymmetry at angle limits: equality at the
negative limit enters the clamp and can clear rotation, while positive
equality does not. The pitch flag uses the high byte of the big-endian angle.
These are fixed original-step calculations, not a variable-delta-time model.

Two native entrypoints connect the recovered stages in the original order:

- `scr::updatePhysicsPrefix` in `src/physics_prefix.cpp` covers the first
  eight driver calls: matrices, wheel coordinates, road sampling, expected
  heights, world transform, wheel speed, view terms and suspension response.
  It stops inside suspension immediately before the countdown call at
  `0x62042`. Sampling uses the previous speed magnitude, and wheel speed uses
  the previous contact flag, matching the original update order.
- `scr::updateMotionTail` in `src/motion_step.cpp` covers the last five calls:
  damping, angular integration, angular transformation, world-velocity
  integration and position/angle integration. Earlier acceleration and
  steering results remain explicit inputs.

The combined oracles execute the actual, unmodified driver sections at
`0x6185c` and `0x618ae`. Registers and flags flow continuously between calls;
they are not reset at each function. D1/D2 high-byte preconditions are set once
at entry, and all other initial registers/flags are randomized. The comparison
checks all 512 KiB of RAM except the test's vectors, sentinel and stack, with
permitted writes and boundary/return behavior checked separately.

Individual motion-transform tests cover **2,019,256 cases**, wheel-speed/damping
tests **1,157,446**, and integration tests **1,466,186**, all with zero mismatches.
The first-eight-call sequence passes **1,281 cases**, including the race input,
sequential state updates and randomized replays. The menu captures have no
loaded track and are explicitly skipped for this prefix. Integration also has
**1,024 separate original-code probes** demonstrating its D2 high-byte
precondition; these are not counted as native differential cases.

The five-call motion tail passes **10,627 cases**: three captured input sets,
384 angle-boundary cases, 8,192 randomized inputs and 2,048 sequential updates.
It also verifies all ten driver/helper calls and their exact stack writes.
Across this milestone, **4,654,796 differential comparisons pass with zero
mismatches**. These are bounded stage/sequence checks, not complete driving
sessions or exhaustive tests of every input combination.

Reproduce all motion stages and both connected sequences:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_motion_stages.py
```

The wrapper verifies the retained source/payload, code/table spans, captures
and pinned Musashi sources, rebuilds, and records all results and source hashes
in `analysis/motion-stages/check-results.json`. Detailed comparisons and original
listings are in `analysis/motion-transforms/`, `analysis/wheel-motion/` and
`analysis/motion-integration/`. REA evidence, snapshot and finding index are in
`analysis/rea/motion-stages-*`. Musashi remains a test-only dependency.

These earlier prefix/tail entrypoints remain useful stage tests. The complete
driver, including the intervening producers and conditional scheduling, is now
connected and verified below.

### Complete native physics update

`scr::updateGamePhysics` in `src/physics_update.cpp` implements the complete
original driver at **`0x6185c–0x618ce`**, through its final return. It accepts
typed game inputs, local asset data and persistent state, and returns an ordered
list of audio-register writes. The production `scr_physics` library has no
CPU, RAM image, dispatcher, WinUAE or Musashi dependency.

The newly recovered pieces are:

| Stage | Original runtime range, end exclusive | Native implementation |
| --- | --- | --- |
| Steering response | `0x61012–0x6125a` | `steering_response.cpp`: bank/direction handling, steering force, automatic yaw correction |
| Car orientation | `0x620b8–0x62138` | `car_dynamics.cpp`: adjusted inputs and gas accumulator |
| Roll transition and steering multiplier | `0x6217a–0x621f4` | `car_dynamics.cpp`: contact-dependent roll/slip response |
| Collision/drag adjustment | `0x621f4–0x622dc` | `car_dynamics.cpp`: ground, countdown, wreck and drafting paths |
| Remaining suspension body | `0x62042–0x620b6` | `suspension_effects.cpp`: countdown, collision application and impact sound requests |
| Countdown, lift and roll helpers | `0x5b32e–0x5b506` | `suspension_effects.cpp`: all crane/release branches |
| Multiplayer side, random byte and message helpers | `0x58758–0x5879e`, `0x62574–0x625a2`, `0x5e4ec–0x5e502` | `suspension_effects.cpp`: native state updates |
| Opponent collision forces | `0x63e2e–0x63ede` | `suspension_effects.cpp`: applies/clears forces, changes opponent speed and wheel velocities |
| Sample request | `0xf362–0xf3ee` | `audio_playback.cpp`: audio bookkeeping and ten ordered 16-bit Paula writes |

The update preserves the original order:

```text
matrices -> wheel coordinates -> road sampling -> expected heights
 -> world/local transform -> wheel speed -> view terms -> full suspension
 -> [when frame-processing flag is nonzero:
       orientation -> steering -> secondary transform -> collision/drag
       -> damping -> angular integration -> angular transform]
 -> world-velocity integration -> position/angle integration
```

On the skipped path, retained acceleration and transformed angular velocity
still feed the final integration calls. Native state also preserves the
original scratch aliases, including the shared steering/sampling configuration,
the suspension/steering scratch word, and the saved yaw spring **before**
opponent forces are added. These details matter across successive updates.

Original behavior is retained where the enhanced disassembly differs:

- Steering checks the runtime longword at `0x64aec` against `0x9cedcd02` and
  clears yaw acceleration when it differs. All three captures contain that
  value; the disk contains `0x2f76ea80`. It is mutable, runtime-patched state,
  not an immutable table. The enhanced source comments out the check.
- Automatic yaw correction uses the original direct increment. The enhanced
  source adds timestep scaling. Countdown roll still uses the original
  **238/256** factor, with original timer/frame-throttle behavior.
- Impact volume shifts a byte before clamping it. Its overflow is preserved.
  Opponent and impact sounds retain their independent cooldowns and request
  order. Sample 3's volume byte is persistent mutable state.

The new original/source comparisons examine **544 instructions**, with every
operand resolved and **22 explicit differences** recorded. They include shared
dependencies previously recovered. REA/Ghidra findings are retained under
`analysis/rea/full-physics-*` and indexed in `analysis/rea/index.json`; Capstone
MC68000 and execution of the original bytes provide independent checks.

Verification passes with **zero mismatches**:

| Differential suite | Cases |
| --- | ---: |
| Steering | 954,757 |
| Orientation, roll, multiplier and drag | 1,942,508 |
| Suspension effects, countdown and opponent collision | 1,592,819 |
| Sample playback requests | 141,096 |
| Complete continuous physics driver | 18,961 |
| **This milestone total** | **4,650,141** |

The complete-driver oracle executes the unmodified original code from entry
through its final RTS. No callee is stubbed, no physics stage is skipped by the
harness, and registers/flags flow between calls. The original D1/D2 high-byte
preconditions are set once at entry. All 512 KiB of RAM are compared except
the test vectors, return sentinel and stack; original writes are bounded and
the returned stack pointer is checked. Audio calls execute their actual
original instructions and their ordered hardware writes are compared too.

The 18,961 cases include the captured race inputs, all countdown byte values
under eight scheduler/network combinations, boundary inputs on all **44 loaded
track segments**, 8,192 randomized states and **8,192 sequential native ticks**.
Those sequence tests keep native state independently across ticks: they never
reconstruct it from emulator outputs. There are **10,783 full-path** and
**8,178 skipped-path** updates, and **4,476 original audio requests**. The two
menu captures have no loaded track and are explicitly skipped for this suite;
the individual new stage suites test them. All earlier regression suites pass.

Reproduce with the retained local assets and captures:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_physics_update.py
```

The wrapper verifies the original disk/payload, reference source, code/table
spans, captures and pinned Musashi sources, rebuilds, and records source and
executable hashes in `analysis/physics-update/check-results.json`. Compiler
output is in `analysis/physics-update/build.log`. Detailed source comparisons
and original listings are under `analysis/steering-response/`,
`analysis/car-dynamics/`, `analysis/suspension-effects/` and
`analysis/audio-playback/`. These generated artifacts remain ignored by Git.

This milestone established the **complete synchronous physics-driver contract**
for the tested inputs. Its standalone API accepts controls, track
progress, collision-detection results and other external state. The native
collision stage applies supplied forces. The next section adds the input and
contact producers. Audio is represented by register events, not
rendered waveforms, and interrupts are not interleaved in these tests. The
sequences are scripted tests, not recorded human-driven laps. Full-game timing,
rendering, all tracks and every combined input remain outside this proof.

## Outer-loop controls, collision detection and driving replay

Recovered native C++ now produces local keyboard/joystick controls, steering
and boost commands, track progress and road boundaries, opponent distance and
contact forces, engine/debris/damage feedback, frame throttling and lap timing.
`src/player_controls.*`, `track_progress.*`, `opponent_collision.*`,
`driving_effects.*` and `race_timing.*` contain the typed implementations.
The native library has no CPU emulator dependency.

The replay compares the original local Practice gameplay interval
**`0x5d47e..0x5d4ba`**, one original physics update per iteration. Both sides
start from the captured race state, then evolve independently. Only controller
inputs are supplied per tick: position, speed, track segment, geometry inputs,
RNG and scheduling state are never copied from original results into the native
side. RAM-address adapters live in `tests/`; production algorithms use named
state and data structures.

Four authored input sequences pass **1,920 updates with zero mismatches**,
covering acceleration/release, braking, steering, boost and off-track driving.
They execute **214,506,126 original instructions** and compare **14,160 ordered
sample-audio register writes**, alongside the declared gameplay projection.
The five new stage suites pass approximately **2.99 million cases**. The
existing complete-physics milestone's **4,650,141 cases** remain separate
regressions. Exact case counts, capture identities, source/executable hashes,
per-stage comparison addresses and per-tick CSV traces are retained in the
generated reports.

Graphics contain two subtle simulation dependencies: particle effects consume
the shared RNG, and damage progression reads the damage bar's pixels. Hole
sprites overwrite that same strip. The native model preserves the small
eight-row damage canvas and its masked hole draws, including both buffers,
without needing a game renderer. Wheel drawing also shifts a bounce byte needed
on the next update.

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_driving_replay.py --run-leaf-tests --native-only-verify
```

`driving_replay_native.exe` also runs the same authored inputs as an independent
native executable linked only to `scr_physics`. It produces matching gameplay
traces without Musashi. The comparator stops at the first divergence and saves
the tick, stage/address diagnostic and both local state images. See
[the replay guide](docs/driving-replay.md) for commands and input format.

This is a **headless Practice-core replay**, not a finished native game or a
recorded human-driven lap. It stops before outer pause, restart, race-end and
buffer-swap gates, and resets original CPU registers at each interval entry.
The original CPU renderer runs in the comparator with blits reported complete;
actual blitter operation, interrupt timing, pixels and audio waveforms are
outside the comparison. Draw Bridge animation is now included at its original
outer-loop boundary. Opponent
contact/distance has separate differential coverage; autonomous opponent driving
and network mode are not substituted into the Practice replay.

### Restart, race end and complete-lap replay checks

`src/race_transitions.*` and `src/race_restart.*` now recover the original
local restart/end decisions, buffer swaps, pause bookkeeping and restart
initializer. The native reset retains the complete cleared vehicle span,
including its previously unnamed words. It selects a safe segment, places the
car using recovered geometry and physics, and restores suspension and chain
state. Original quirks include the signed wrapped restart countdown, clearing
only the high byte of wheel speed on race end, and six-frame buffer waits.
The original initializer stores its road height at `0x1bc60` and uses chain
velocity 8; the enhanced reference differs on both.
Practice exit cleanup is also recovered through its return at `0x5d724`.
The original RNG save writes its tail over the second byte of the saved seed;
the native API preserves that overlap.

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_driving_replay.py --full-loop `
  --run-leaf-tests --native-only-verify `
  --output analysis/driving-replay/full-check-results.json
```

Full-loop mode continues to the next main-loop entry or terminal input/fade
boundary, preserving original CPU context between iterations. It includes the
two restart warmup frames, their physics/timing updates and buffer swaps, and
the engine restart. The original/native comparison still uses explicit gameplay
projections and ordered sample/audio-disable writes. A headless boundary services
frame waits; real interrupt timing, renderer pixels and menu return are outside
this replay. Pause routines have separate coverage; the runner currently takes
an unpaused capture and has no pause/unpause timeline.

The seven leaf suites pass **3,051,186 cases with zero mismatches**, including
58,830 transition/exit cases and 704 restart-initializer cases. Full-loop replay
passes the four earlier sequences across **1,920 updates and five restarts**;
the separate native executable produces matching traces. The one-update quit
scenario also matches. Source comparisons cover 183 transition/exit instructions
and 132 restart instructions, with all differences from the enhanced reference
recorded explicitly.

The trace now records lap counters, crossing latches, race-end actions and
restart counts. `--full-loop --require-complete-lap` requires two observed
crossings with every track segment in order, latch disarm and no intervening
restart. A first start-line crossing alone cannot satisfy it. The current
retained authored lap regression completes a full cycle in **506 updates with
zero mismatches and no restarts**. Its crossings occur at ticks 24 and 505,
with all 44 segments in order and latch disarm at tick 272. Its controls were
selected by a native feedback experiment, then replayed independently against
the original; no position or velocity was injected. No human-driven lap or real
interrupt timeline has been captured. See the [replay guide](docs/driving-replay.md)
and [lap evidence guide](docs/replay-lap-validation.md) for the exact contract.

The longer `tests/replays/laps/authored-race-1463.csv` regression also passes:
**three complete laps and the race-end transition in 1,463 updates**, with
zero mismatches. Crossings occur at ticks 24, 505, 961 and 1,415; the original
enters its end-message input wait at tick 1,462. The separate build with
`SCR_BUILD_ORACLE=OFF` produces an identical gameplay trace. Both retained
control files have adjacent provenance JSON, and the original/native report is
`analysis/driving-replay/lap-regressions/check-results.json`.

### Watch the verified race with native rendering

Double-click `Watch Native Replay.cmd`, or run:

```powershell
.\scripts\start-native-replay.ps1
```

The launcher verifies the local asset and original-trace hashes, builds the
native Windows viewer, and checks all 1,463 gameplay and rendered frames before
opening playback. It presents interpolated camera/suspension motion at 60 FPS
while preserving verified physics ticks at their original 0.12-second spacing.
Space pauses, the arrow keys step, the timeline seeks, and I toggles exact ticks.
The viewer uses recovered road geometry, angular projection and original
cockpit assets; its executable contains no CPU emulator. It draws raised-road
sidewalls and the original mountain silhouettes, with segment colors and
screen roll. Dense near-track geometry, visibility, painter order and alternating
edge highlights now follow the original routines. A live FPS counter measures
window presentations. See the [viewer guide](docs/native-replay-viewer.md) for
controls, exported frames, differential evidence and rendering boundaries.

### Drive the native port

Double-click `Drive Native.cmd`, or run `scripts/start-native-game.ps1`, to open
the original native main menu. Choose **Practise**, a division and a track;
Left/Right rotates the original overview and Return/Space/controller A starts
the race. Escape leaves the race through its original exit gates, returning to
track selection; **Cancel** returns to the main menu. The game image uses the
original assets, font, palettes and selection highlights. Initial mode/name
entry, Hall of Fame, season and load/save routes remain outside this milestone;
unported routes produce an explicit host notice. See [native game](docs/native-game.md)
for fidelity checks, controls and boundaries.

`scripts/start-native-race.ps1` retains the direct-race development viewer.
The launcher imports a local data pack from the supplied ADF when needed and
constructs a fresh Practice race using native initialisation. It needs no saved
Practice state, Kickstart ROM or emulator. All eight tracks are supported;
`-Track 0` is the default and `-Track 5` selects Draw Bridge. Its native animation
updates both road geometry and physics at the original rate. The original
preview holds a raised bridge pose; movement begins during the race. See
[Draw Bridge](docs/draw-bridge.md) for timing and comparison evidence.
Click **Drive** or press **P** to begin. Arrows/WASD steer and operate gas/brake;
Space boosts. Xbox controllers use the left stick/D-pad, RT/LT for gas/brake and
A for boost. Physics retains fixed 120 ms steps with smooth 60 FPS presentation.
The original engine and effects now play through native stereo audio. M mutes,
P pauses, R resets, and focus loss pauses automatically. Gas remains latched
after release, as in the original; use brake to slow down. Inputs are recorded
under `analysis/live-driving/`. See [native driving](docs/native-driving.md) for
controls, controller detection and verification.

Audio uses the recovered 50 Hz engine interrupt and original sample completion
handlers, with separate audio state and 48 kHz native output. Physics remains at
120 ms per update and rendering at 60 FPS. The recovered audio routines pass
618,645 original/native comparisons; the complete race also exports checked PCM
without changing any gameplay trace column. See [native audio](docs/native-audio.md)
for sample provenance, reproduction and fidelity limits.

The local pack and provenance manifest live under ignored `analysis/assets/`.
Only extraction/decoder code, checksums and tests are tracked. A pre-commit hook
and source-only CI check reject original media, asset packs, generated binaries
and bulk encoded data. Fresh-start checks cover 4,096 updates across all eight tracks,
including original-code comparison and deterministic reset. See
[native startup and assets](docs/native-startup.md) for import, pack format,
reproduction and Git safeguards. Captures remain test references for the separate
verified replay mode.

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

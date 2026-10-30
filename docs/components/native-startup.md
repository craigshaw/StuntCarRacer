# Native startup and local assets

`Drive Native.cmd` opens the [native original game](../playing.md), including
startup, menus, Practice and local championships. Fresh race state is constructed
from the original disk data. The direct-race development command below bypasses
menus and selects Practice. The native application requires one local asset
pack; it does not load a ROM, savestate,
WinUAE or a CPU emulator. The original captured replay remains a separate
regression test and is still available through `tools/replay/Watch Native Replay.cmd`.

## Import and launch

For ordinary play, open `dist/Stunt Car Racer.exe` after [building and staging](../development.md#build-test-and-stage), and choose the supported ADF
in its launcher. Native import performs the same identity checks and extraction
recipe as the development tool below. The application retains the pack and
provenance under `%LOCALAPPDATA%/Stunt Car Racer/assets/`; the ADF remains read-only.
The launcher is available before any game data is loaded, so missing or damaged
assets can be repaired in the UI. See [launcher behaviour](../playing.md).

Place the supported original ADF in the project root, then run:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/import_native_assets.py
.\scripts\start-native-race.ps1
```

The development launcher creates the pack automatically when it is absent. An existing
pack can launch without the ADF present. To import a disk at another path, use
`-Adf <path>` on the launcher. `-Track 0` is the default; all eight tracks can
be selected, including the moving Draw Bridge (5). Opponent AI, menus and
competition flow are outside this direct-race startup path. The window starts paused;
P/controller Start begins driving, R reconstructs a fresh race, and M mutes.
Physics remains 120 ms per update, presentation 60 Hz and engine audio 50 Hz.

The two generated files are:

- `analysis/assets/stunt-car-racer.scr-assets`: local original data pack.
- `analysis/assets/stunt-car-racer.scr-assets.json`: source hashes, offsets,
  extraction recipe and pack identity.

The ADF is read-only. Its SHA-256 is
`548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
The pack's source image is the verified 408540-byte disk slice at 0xdc00, loaded
at 0xe700, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.

## Pack format and startup

Version 1 keeps the original source image as one addressable data section,
including the code bytes alongside its assets. Those bytes are never executed
by native startup. Keeping this first format close to the verified disk mapping
lets the existing decoders use original data offsets without distributing
generated assets. A future pack version can store separate typed asset sections.

All integers in the 88-byte header are little endian:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | `SCRDATA1` |
| 8 | 4 | Format version 1 |
| 12 | 4 | Header size 88 |
| 16 | 32 | Original ADF SHA-256 bytes |
| 48 | 32 | Source image SHA-256 bytes |
| 80 | 4 | Source image load address 0xe700 |
| 84 | 4 | Source image size 408540 |
| 88 | 408540 | Original source image |

The loader rejects unknown versions, wrong extents, changed source identity or
changed payload content. It does not fall back to a capture. Native startup then:

1. Creates zeroed 512 KiB memory and maps the data section at 0xe700.
2. Rebuilds graphics objects, font patches, framebuffers and signed PCM/engine
   sample banks with `initializeNativeAssets`.
3. Reproduces the successful-load validation/checksum writes. This includes
   the runtime value required by the original steering gate.
4. Clears and initialises fresh game/player data using the recovered reset
   routines. This precedes track construction because the reset clears those
   mutable track tables.
5. Decodes the selected track, constructs interpolation/grid data and generates
   the road cache with `initializeNativeTrack`. Draw Bridge receives the original
   first geometry update and a cache rebuild before race initialization.
6. Initialises the local player, fuel, damage, timing and vehicle placement, then
   runs the recovered startup frames with `initializeNativeRace`.

The existing gameplay adapter still serializes native typed state into the
original memory layout. This layout is an internal compatibility format; it is
constructed by native code, with no saved memory image as an input. Rendering
and audio consume the resulting state as before.

## Checks

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_startup.py
# Optional original-code proof; needs the prepared local Musashi test source:
& $AmigaPython scripts/check_native_startup.py --verify-original
```

The ordinary check has no capture, ROM or original-CPU dependency. It checks all
eight tracks for 512 updates each, rendering, horizontal vehicle movement,
audio generation, deterministic reset and rejection of ten corrupt pack cases.
It also requires actual opposite left/right yaw responses from the same moving
fresh state, so matching original execution with disabled steering cannot pass.
Results and a source-identified build receipt stay under `analysis/native-startup`.
`--skip-build` rejects a stale source or executable identity.

The optional original check executes the unmodified original game loop from
each native-created initial state. Across 4,096 updates, it compares the declared
gameplay and bridge projections, automatic restarts and ordered sound commands. It
also compares each complete scene trace with the original execution. The old
1463-update, three-lap captured replay and 60 Hz rendering regressions still pass.
The deeper proof is retained separately in `original-check-results.json`, so
ordinary launches can refresh their checks without replacing that evidence.

Startup components have independent checks: all 52 graphics objects and initial
memory, 24 track/difficulty cases across all 8 track databases, and 224 load/reset
cases. See [native assets](native-asset-initialization.md) and
[race initialisation](native-race-initialization.md) for code ranges, hashes,
evidence and the distinction between memory results and hardware timing.

## Source-control safeguards

Only code, extraction recipes, format definitions, hashes, documentation and
authored test inputs belong in Git. Original images, sounds, fonts, palettes,
track layouts, memory dumps and asset packs remain local, including when encoded
as text or C++ arrays. Generated previews and test artifacts also stay ignored.

`scripts/check_repository_assets.py` examines actual indexed blobs, so a forced
addition or a staged binary hidden behind a clean working file is checked. It
rejects generated directories, media/pack/binary file types, non-text content,
media signatures, bulk encoded/numeric data and unreviewed file types. These
checks catch common accidental additions; code review remains necessary for
arbitrarily disguised data. `--worktree` also checks non-ignored new source files.

The local hook is enabled for this checkout with:

```powershell
. .\scripts\env.ps1
git config core.hooksPath .githooks
& $AmigaPython scripts/check_repository_assets.py --worktree
```

Other checkouts must enable the hook themselves (on Unix, also make
`.githooks/pre-commit` executable). GitHub's `source-only.yml` workflow runs the
same index check and synthetic policy tests without requiring game data.
No generated pack or original asset is included in the source repository or CI.

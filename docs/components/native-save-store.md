# Native save-file backing

This guide describes retained multiplayer/manual transport and baseline regression
instances. The player uses [native career persistence](native-career-persistence.md)
for single-player autosaves and shared Hall records; legacy HALL import/export is
disabled there. The manual file is never an input to the new stores.

`NativeSaveStore` supplies the local storage used by the original native
Load/Save/Replay flow. Its file contains the original 512-byte directory and
thirty 512-byte transfer blocks, in that order: 15,872 bytes in total. These
correspond to the original logical save-sector indices 22 and 23..52. The
directory's slot names, flags, validation value and selected/next-save indices
retain their original byte positions. Career, Hall and multiplayer data keep
their [original payload encodings](native-save-state.md).

The native game defaults to `working/saves/stunt-car-racer.scr-save` relative to
the project launch directory. `start-native-game.ps1 -SaveFile` and the native
`--save-file` argument accept another path with the `.scr-save` extension.
The extension is required so save writes cannot target the original ADF, ROM or
asset pack. Missing files present a blank save medium to the original menu.
Unexpected file lengths report an I/O failure and leave existing bytes intact.

Saving writes the complete candidate directory and slot data to a sibling
temporary file, flushes it with `FlushFileBuffers`, then uses
`MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` to replace the committed file.
The slot payload and its directory entry are therefore committed together.
Failed writes or replacements leave the previous save file intact. Temporary
files left by an interrupted process do not become active saves. These host
storage choices replace floppy transport; they do not change normal decoded
game state or the original retry/Escape prompts.

The imported ADF, ROM and asset pack are read-only inputs throughout this flow.
Save files and their temporary siblings remain ignored, and the Git asset
safeguard also rejects forced additions of `.scr-save` files.

## Verification

`native_save_store_tests` writes all thirty slots, constructs a new store after
each write and verifies every earlier payload. It checks directory-only updates,
read-only-file replacement failure, stale temporary output after interruption,
four invalid lengths, invalid slot indices and refusal of original-media paths.

`native_save_session_tests` exercises original startup, real slot editing and
menu routes with separate store instances and fresh game sessions. It saves
and loads careers, restores both car contexts, imports and exports Hall banks,
restores three local multiplayer drivers, drives three loaded careers, checks
normal/multiplayer/F1 Replay, and exercises cancellation, retry, abort and corrupt
payload rejection. The optional full check compares each loaded-race trace
against the original game loop.

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_game.py --verify-original
```

Reports, generated save media, screenshots and source/executable identities
remain under ignored `analysis/native-game/`.

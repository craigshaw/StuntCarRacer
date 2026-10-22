# Native player-name entry

`app/native_player_name.cpp` ports the disk build's raw input scanner and name
editor to C++. The native game does not execute the original 68000 code. Names
and the keyboard tables are read from the locally extracted asset image; no
original graphics, fonts, strings or executable bytes are checked in here.

The evidence is the payload at disk offset `0xdc00`, loaded at `0xe700`, length
408540, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
REA's original name-entry dossier is retained locally as
`analysis/rea/opening-2.json`, Evidence
`ev_01868ff2a11dcd206bbfae2022d06f665e6c0deab2b1cc81bd437570651e1f6a`.
Its instructions were cross-checked with Capstone's MC68000 decoder and with
execution of the unchanged original routines in the verification-only Musashi
build. The enhanced Vesuri source is a navigation aid: its default `Player`
name and extra key-release loop are absent from this disk.

## Original behavior

The original `runPlayerNameEntry` at `0x5b648` draws a prompt, then calls
`inputPlayerName` at `0x5b6de`. An initial release gate discards input until the
scanner reports no input. It then starts at position zero, with a 12-character
limit and record offset `(11 - additionalPlayerCount) * 16` within the name
array at `0x1ecab`. The editor's current position is held separately from RAM,
matching the original CPU register; the declared RAM scratch fields retain
their original addresses and meanings.

- Letters retain their case. Shift selects the original uppercase table through
  pointer `0x1b894`; the ordinary table is selected through `0x1b890`.
- Accepted punctuation is `.`, `/` and `:`, together with digits and spaces.
  A space at position zero is ignored. Trailing spaces are retained.
- Twelve characters fit. Further printable characters are consumed and ignored.
  Backspace still works when the name is full.
- Backspace at position zero does nothing. Otherwise it moves back one position
  and emits the original erase character `0x7f`. The old stored byte is replaced
  only by subsequent input or final padding.
- Return, keypad Enter and controller fire finish the entry and pad the remainder
  with spaces. An empty name starts another attempt and waits for input release;
  it does not acquire an invented default name.
- Escape and directional input do not cancel ordinary name entry. The generic
  text editor's original signed directional flag at `0x62bb5` is also supported
  and verified, including its distinct Escape/up/down/left/right result bits.
- The generic editor preserves its original uppercase conversion for record
  offset `0xc0`; normal player records do not use that conversion.

The scanner at `0x5985c` searches raw key bytes from code 127 down to zero. Only
the exact pending value `0xb3` is accepted. Left and right Shift (`0x60`, `0x61`)
are excluded from that search and remain held; either nonzero Shift byte chooses
the uppercase table. The highest pending ordinary key is consumed first and its
byte is cleared. Caps Lock does not toggle case in this scanner.

If no ordinary key is pending, controller input takes priority in the original
order: up, down, left, right, fire. These become original encoded raw/character
pairs, including `0x440d` for fire. The normalized host control bits are translated
back to the original active-low joystick byte at `0x1b88e`.

There is no software key-repeat clock or per-character animation delay. Original
`waitForInputPress` at `0x59850` waits for availability only. A keyboard event is
consumed once; another key-down interrupt is needed for another character.
Controller input remains available while held. The host supplies key-down and
key-up transitions as Amiga raw codes and excludes host autorepeat. Its event
polling does not depend on the 120 ms racing physics clock.

## Drawing boundary

`NativePlayerNameEntry::begin()` and `update(controls)` return drawing events.
`beganAttempt` requests the original prompt/cursor placement, `characters` carries
accepted glyphs and erase commands, and `complete` requests the original exit
color/offset restoration. `retriedBlank` distinguishes the prompt drawn after
empty-name submission. These events drive `NativeOpeningNameScreen`; input code
does not mutate the presentation's cursor, font or screen buffers. Padding the
stored name does not draw extra spaces on screen.

## Verification

Build `native_player_name_tests` with `SCR_BUILD_ORACLE=ON` and the repository's
pinned local Musashi directory, then run:

```powershell
& build/windows-oracle/Release/native_player_name_tests.exe `
    analysis/reference/vesuri/offline-extracted-payload.bin
```

The comparison executes `0x59862` after the hardware joystick read, supplying its
declared active-low result, and executes unchanged `0x5b752` after the blocking
input wait. The original character renderer executes normally during editor
checks; glyph calls are observed, not stubbed. Complete input sequences execute
the original `0x5b6de` with keyboard-interrupt snapshots injected at scanner calls.
This bounds the claim to scanner/editor state, stored names and emitted glyphs;
the independent opening renderer comparison checks screen pixels.

The current receipt in `analysis/player-name/report.json` records zero
mismatches across 24,576 scanner cases, 12,300 editor-loop cases and 48 complete
input sequences, comparing 6,343,692 bytes and executing 7,744,719 original
instructions. Coverage includes every raw key, every controller combination,
both Shift keys and nonstandard nonzero Shift states, simultaneous keys, table
pointer indirection, every possible character byte at empty/near-full/full
positions, special directional mode, all eight player slots, blank retries,
held fire, ignored navigation, mixed case, spaces and full-name editing.

# Native Hall of Fame

`makeNativeHallOfFameImage` draws the original 320×200 record screen from the
local asset pack and session records. It uses the background and palette at
`0x30bf0` and `0x30bd0`, the original primary font, headings at `0x5ec92`, and
track abbreviations at `0x5fde0`. These are runtime addresses in the 408,540-byte
payload loaded at `0xe700`; payload offsets subtract `0xe700`, and source disk
offsets additionally add `0xdc00`.

All eight rows retain their original order, colours, spacing, twelve-character
names and BCD times. Unset records use the original seven hyphens. Context zero
uses the first lap/race record bank; every nonzero context uses the second and
adds its original heading. See [record handling](native-hall-records.md) for
the entry format and competitive record updates.

Selecting the first main-menu row fades to the Hall palette's entry zero,
prepares the selected records, draws the complete screen, then fades it in.
After release and the original five-unit debounce, a new fire press fades back
to the empty division menu frame before the main options repaint. The native
session reuses the independently verified palette and fire-wait routines.
Their existing host timing limits apply: Amiga rendering cost, bus contention
and incidental idle-poll RNG advances are not reproduced.

## Verification

`native_hall_of_fame_tests` executes the original `0x5fbc6..0x5fdc8` setup and
rendering instructions without changing code bytes. The harness supplies the
interrupt countdown effects at the original fade spin waits. It compares all
64,000 indexed pixels and sixteen palette words for each of 640 screens:
five context bytes and 128 combinations of names, valid times and unset
records. All 40,960,000 pixels and 10,240 palette words match. It also verifies
that the native renderer leaves gameplay memory unchanged.

The regular game check visits the Hall before and after racing on each track,
checks held fire and debounce, and checks the return to the same main menu.
It also visits after each supported original name-entry setup. Run both pixel
and record comparisons with the full game regression:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_game.py --verify-original
```

Source identities, executable identities, reports and screenshots remain under
ignored `analysis/native-game/`. The source payload SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.

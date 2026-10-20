# Original save state and Replay

The native save codec ports the original logical state formats. Disk blocks
are supplied by the native save store; neither saving nor loading changes the
input ADF. The original CPU is linked only into the verification executable.

## Payloads

The original transfer buffer is 512 bytes at `0x7a41a`. Normal careers copy the
first 192 bytes of `0x7a01a` into that block. Their meaningful representation
contains twenty-seven RNG search positions, two five-byte reversed RNG seeds
and twenty-seven search counts. Decoding regenerates the twenty-seven bytes at
`0x1c9b6`: the local player's twelve name characters, the twelve-driver order,
division, remaining damage allowance and league context. The name uses the
original XOR preparation. No host serialization replaces that representation.

The `HALL` command transfers both 256-byte record banks. Each is processed by
the original byte substitution routine, retaining its reserved bytes and
shared cipher index. Loading merges complete sixteen-byte entries only when
their minute/second/hundredth tuple is strictly better; equal records retain
the current holder. `MP` retains the original names, score tables, damage
allowances and local-player count in its separate obfuscated block.

The table generator and substitutions are reproduced from the actual
MC68000 routines at `0x5f708..0x5f988`. Their validation markers are format
checks, rather than a host checksum. The native loader also checks decoded
indices and values before applying them to active game state.

## Transaction and Replay order

`beginNativeSaveTransaction` sets the original read/write flag, snapshots the
256-byte career buffer and its encoded flag, then prepares the current career
once. Opening the slot dialog therefore retains the encoder's original RNG
advancement. `makeNativeSavePayload` consumes that prepared buffer.
`restoreNativeSaveTransaction` restores the original career buffer and flag
after normal career saves or cancellation. The original nonzero HALL/MP mode
bypasses that restoration. Neither path rewinds RNG calls that already ran.

`applyNativeSavePayload` runs transfer, decoding and validation on private RAM.
A rejected payload leaves the active memory unchanged. A successful career
load retains the decoded state; `HALL` merges records and `MP` restores its
original local-player data. Presentation and local file transactions belong
to their separate menu and store components.

Replay resets the original game tables, then requests a decode of the retained
career. Its original decoder accepts a nonencoded buffer without changing the
career. An encoded buffer with invalid search positions or final RNG seed
returns carry set. That failure, or the original multiplayer branch, falls
back to fresh game data. The caller then presents the original roster route.
This command replays the retained season setup. Holding F1 follows the original
mode/name startup route while retaining the session's record banks.

The original race record store at `0x5f25a` returns directly after writing the
in-memory tables; it does not write a save block automatically.

## Evidence and verification

The implementation uses the payload loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Independent MC68000 disassembly confirms the operands and branch order.
Retained REA dossiers are in `analysis/rea/save-league-codec.json`,
`save-payload-dispatch.json`, `save-career-transfer.json` and
`save-main-menu.json`. Ghidra reports a fragmented dispatch body containing
`0x5f04a..0x5f073` and `0x5f374..0x5f4b5`; the separately decoded and executed
substitution and multiplayer callees cover the remaining format behavior.

`native_save_state_tests` compares complete native RAM and carry outcomes with
the untouched original routines. It exercises encoding, decoding, corrupt
states, all three payload modes, snapshot/cancel restoration and Replay's
decode/fallback paths. Host roundtrips and rejection tests additionally check
that a rejected payload cannot partially replace live state. Original menu
pixels, palette work and the caller's delay are verified by the menu suites.
The codec run passes 800 original comparisons, 419,430,400 RAM bytes and
144,895,633 original instructions with zero mismatches. Thirty-six host
roundtrips cover all three payload types; thirty-nine rejection cases retain
the active state byte for byte.

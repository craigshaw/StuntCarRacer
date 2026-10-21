# Original single-player career state

Single-player careers retain the original six-round fixture schedule in all
four divisions and both league contexts. Four fixtures involve the local
driver; two are computer-only. Every completed fixture scores all divisions,
retaining the original AI skill draws, outcome bits and random tie breaks.
The current division determines the two alternating tracks and their original
configuration bytes.

F1 at a matchup follows `0x5d000`: set race mode and outcome to `0xc0`, then
score every remaining fixture, including human fixtures, without entering their
races or result pages. Promotion and the normal season-end screens follow
after the six-round remainder has finished. The current round is included in
that remainder.

The original end-of-season routine copies the final standings and exchanges
division winners and last-place drivers before finding the local driver's
next division. A normal-league top-division win enters the Super League at its
bottom division and resets the driver order. A Super League championship keeps
the top division and context, replenishes the damage allowance and leaves the
award-image flag at `0x80`; it does not request the ordinary division-winner
award bitmap.

Competitive races retain their remaining damage allowance; Practice restores
the pre-race value. Entering the bottom division and the two championship
branches retain the original replenishment rules. Career saving preserves the
name, driver order, division, damage and league context in the original encoded
representation. Saving restores the previously retained Replay buffer after
writing; loading a saved career retains the newly loaded buffer. Replay
therefore restores the saved season setup according to that original flow.

## Verification

`native_career_state_tests` compares untouched MC68000 instructions with native
RAM at the actual caller boundaries. It passes 2,392 comparisons and
1,254,096,896 compared bytes with zero mismatches, including:

- 576 F1 remainders across every round, division, context and local division
  position.
- 264 targeted promotion/relegation and championship states across all eleven
  damage values, including deterministic normal and Super League champions.
- 176 competitive/Practice damage exits.
- 96 independently evolved complete seasons containing 384 human and 192
  computer-only fixtures, with 2,366 original random tie-break draws.

The separate native menu-flow scenarios pass sixteen seasons and sixty-four
synthetic races. Eight consecutive winning seasons progress from the normal
bottom division through both championships. Eight losing seasons check
relegation and the bottom-division boundary. Eight Replay checks and eight
fresh-state save/load continuations verify retained setup, name, driver order,
damage, league context and final RNG state.

Human outcomes are injected at the race boundary in these state tests. They
are not claimed as driving replays. The game-session suite separately exercises
native driving and compares its recorded physics against the original.
Drawing, palette and audio calls are skipped at their declared presentation
boundaries; every RAM byte is still compared. No captured RAM or ROM is an
input to the state suite.

## Evidence boundaries

The payload is 408,540 bytes loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
MC68000 disassembly independently confirms the operands and branch order.
The retained REA evidence includes:

| Scope | Evidence ID | Observed body |
| --- | --- | --- |
| Main career caller | `ev_eee0dde04ea961695bd2a629bf3df1e1682dea64d4abc38a413326ff73e0e979` | `0x5cf40..0x5d20b` |
| AI round scoring | `ev_f07d1b2df2e7b2f20976b5bcef1b2f8f871968e832aeec55f305fcbe199eb2e9` | `0x5aa84..0x5aaf3` |
| Handicap/outcomes | `ev_a3b617493c4ecdbf1862c21f6037492022f477afffcdbd411a9ef54ba3469820` | `0x5abd4..0x5acfd` |
| Standings sort | `ev_826040519255c06de1b96ad46d91e9badbfbb4196328063b3e3026ff11d6fd2d` | `0x5acfe..0x5adff` |
| Promotion/championship | `ev_b0f84436b322065334acebb812594cadba5706553ba08f901bc3b115a070c5bf` | `0x64916..0x64ac5` |

Fixture configuration evidence
`ev_035908cf1afe88f9f8d66afdc2f1d32963c3653d303162904360f5bf2bfe741f`
has a fragmented Ghidra body including `0x5ab46..0x5abc7` and the separate
network branch `0x580d8..0x58145`. Only the observed local branch is used here.
The season-end header selects the local division at `0x645c6` before entering
the promotion routine; tests retain that caller preparation explicitly.

Generated receipts and binaries remain under `analysis/career-state/`.

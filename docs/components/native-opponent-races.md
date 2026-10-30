# Native local league races

The original main-menu **Start the Racing Season** route creates genuine
local competitive races. Practice keeps its original track-selection route and
has no injected computer opponent. The runtime reads schedules, traits, names,
track configuration and artwork from the locally extracted asset image.

`native_race_initialization.cpp` recovers the original local league helpers:

| Original address | Native operation |
| --- | --- |
| `0x609ae` | Encode the current league, retaining its random-stream searches |
| `0x5ee68` | Set six local fixtures and reset series progress |
| `0x5ab46` | Select the two drivers from the current division and fixture |
| `0x5aa84` | Randomize skill and score the four divisions' current fixtures |
| `0x5abd4` | Apply the human result or simulate an AI-only result |
| `0x5acfe` | Sort points, wins and original random tie breaks |
| `0x64916` | Promote/relegate drivers and update the local player's division |
| `0x5f25a` | Load/store the selected track's local lap and race records |

Each division has three drivers and six fixtures, alternating its two tracks.
The local driver races four fixtures; the other two are simulated, along with
the other divisions, in the original order. Wins and fastest laps earn the
original two and one points. After six fixtures, the original promotion and
relegation swaps determine the next season. The original second car context
and top-division championship transition remain part of the native state.

The league encoder is relevant even without saving to disk: its searches
consume random bytes. Replacing it with a flag assignment changes later
simulated results. Encoded data remains local runtime memory; native league
loading/saving are handled by the native save components. Network play remains
outside this implementation.

The original F1 shortcut forfeits the remainder of a single-player season.
Holding F1 while confirming a matchup makes
`checkRaceStartReady` at `0x58580` clear its start flag: it tests raw key `0x50`
through `0x612be`, whose pressed value is `0xb3`. The original then branches
from `0x5d0d8` to `0x5d000`, sets race mode and outcome to `0xc0`, and scores
all remaining fixtures without entering another race. Season standings and
promotion/relegation still follow. The native route retains that branch,
including the original RNG calls and results in the other divisions.
This shortcut is separate from Space, raw key `0x40`,
which remains a menu confirmation input in the native host.

Fresh race placement runs recovered `initializeTrackCoordinates` at
`0x63a58`, constructing opponent road support before its original randomized
wheel placement and the two startup warm-up frames. The original local race
configuration, lap counter, start delay and outcome state supply the opponent
simulation; the menu does not manufacture AI parameters.

The menu flow keeps the original released-fire wait between matchup, track
preview, result artwork, new records when present, individual race results,
standings and season changes. The original championship award and the complete
driver roster follow the season-change page when appropriate.
The native competition renderer uses the original menu frame, positioned text,
portrait sheet, alternate name font and result pictures. Asset bytes are read
from the pack and are not embedded in source.

## Evidence and tests

The original source image is the 408540-byte disk payload loaded at `0xe700`,
SHA-256 `508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Raw MC68000 listings are retained under ignored `analysis/opponent-races/`.
REA 6.7.0/Ghidra 12.1.4 evidence is retained in `analysis/rea/`:

| File | Evidence ID |
| --- | --- |
| `opponent-main-menu.json` | `ev_9539d4ee01b56359869005349407aed5b150f2283f6a4e60ba71cc1b0d81b0ba` |
| `opponent-race-transition.json` | `ev_583eaa66cfa2017f27e5bdb87e45c1a812bad5f8cc080ac0fce9011e96c895ea` |
| `opponent-race-config.json` | `ev_035908cf1afe88f9f8d66afdc2f1d32963c3653d303162904360f5bf2bfe741f` |
| `opponent-race-random.json` | `ev_f07d1b2df2e7b2f20976b5bcef1b2f8f871968e832aeec55f305fcbe199eb2e9` |
| `opponent-handicap.json` | `ev_a3b617493c4ecdbf1862c21f6037492022f477afffcdbd411a9ef54ba3469820` |
| `opponent-sorting.json` | `ev_826040519255c06de1b96ad46d91e9badbfbb4196328063b3e3026ff11d6fd2d` |
| `opponent-promotion.json` | `ev_b0f84436b322065334acebb812594cadba5706553ba08f901bc3b115a070c5bf` |

`native_opponent_race_tests` executes unmodified original instructions and the
native helpers from identical synthetic initial states. Its 800 cases compare
419430400 bytes across all 512 KiB, with zero mismatches. The promotion test
skips five drawing helpers, while retaining all gameplay instructions and
comparing the entire resulting RAM. The remaining helpers run to their actual
returns without substituted results. A native menu-flow check also completes
all six fixtures, requiring four human races and the return to the main menu.

`native_competition_render_tests` compares every indexed pixel of 476 complete
screens with original 68000 rendering: 30,464,000 pixel comparisons with zero
mismatches. Coverage includes all divisions, both vehicle contexts, every
driver portrait, entered local names, placeholder and BCD record times, new
record flags, standings, promotion, championship awards, the driver roster,
and win/loss/wreck result pictures. It explicitly checks every finishing place
in both leagues, championship context changes and the Super victory's skipped
award, with 7,616 palette-word comparisons. Palette fades and hardware calls are
excluded from this leaf test; the existing palette oracle tests the fade code.

The [career state checks](native-career-state.md) extend the original-code
comparison across complete seasons, promotion, relegation, F1, damage and
save/Replay continuation. The [career presentation checks](native-career-state.md#season-ending-screens)
retain the old division/context for the completed season while the next roster
uses the newly promoted or relegated order.

`native_opponent_session_tests` runs the real native session from the main menu
through one entire season. The first opponent completes its actual three laps;
the other three human fixtures exit through the original quit path. All 1677
updates run at the original 120 ms physics interval. The test checks matchup,
preview, race, result artwork, individual results, standings, season changes,
roster, counter reset and the return to the main menu. It writes reproducible
initial RAM, controls, independently evolved native traces and screenshots
under the caller's ignored local output directory.

These are differential tests of reconstructed routines and synthetic state
coverage, not claims of recorded original human driving or audio fidelity.

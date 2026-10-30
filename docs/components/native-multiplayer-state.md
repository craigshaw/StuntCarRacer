# Local multiplayer championship rules and screens

The native championship retains the original local schedule for two through
eight named drivers. Each driver races a computer pace car on both tracks in
the selected division. Driver IDs descend from eleven for each track. The
first track uses pace-car slot one, the second uses slot zero. The original
assignment routine copies those cars' names, portrait IDs and traits from the
division's source data, retaining its RNG calls.

The division and league context remain the established career selection.
Loading an original manual career/access save during multiplayer uses the original decoder's
restricted copy: it applies division, damage allowance and context while
retaining the local names and driver order. This provides the original saved
career route to later tracks. The existing `MP` save format retains the local
names, score tables, damage allowances and player count between launches.
The format does not store the four-championship counter or career track access;
a fresh launch starts that counter normally, and loading a career restores its
division and context separately. This does not import a native career checkpoint.

## Race and track progression

The native state helpers follow the original caller at `0x5d014..0x5d208`:

1. Prepare the championship's two pace cars and clear the track's lap/race
   time slots and record holders.
2. Select the next human, pace car, original track ID/configuration and stored
   track records. The setup remains separate from vehicle initialization.
3. Restore the driver's five original control-key bytes before racing. After
   player configuration saves the shared damage backup, load that driver's
   damage allowance. On exit, retain their damage and completed race time;
   incomplete races clear their total-time slot. Restore the shared backup
   and save the driver's control bytes.
4. Score the outcome and show its result pages. The original scorer still
   generates all twelve AI skill bytes, then scores only the selected division
   in local multiplayer.
5. After every driver has raced the track, award its record-holder bonuses and
   show the overall standings. Continue to the other track or the main menu.

The race result contributes a win and fastest-lap counter according to the
original outcome bits. Ranking points are twice the win counter plus the
fastest-lap counter. Sorting compares points, then wins, then descending driver
ID for a complete local tie; it consumes no random tie-break draw.

The track summary additionally gives its best completed race holder one win
and its best lap holder one fastest-lap count, then sorts again. An unset
holder, ID zero, receives no bonus. These bonuses occur only in the end-of-track
summary, when both original summary flags are negative; the per-driver time
table does not award them.

The F1 matchup path retains the original forfeit boundary: it scores and skips
the remaining drivers of the current track with outcome `0xc0`. The track then
ends normally and clears that mode before the second track. Scores accumulate
across championships; after the fourth, `initializeGameTables` resets the
score tables, times-played counters and damage allowances.

## State verification

The original routines are `0x5ab00` for table ranges, `0x5ab46` for local driver
selection, `0x5ee68` for pace-car assignment, `0x5aa84` for scoring,
`0x5acfe` for sorting and `0x5ef8e` for track bonuses. Damage load and exit use
`0x5d87e` and the local branch `0x5d698..0x5d714`; control transfer uses
`0x5faf2`. Native helpers never execute the original CPU.

`native_multiplayer_state_tests` compares untouched MC68000 outputs against
native RAM across every player count, division and league context. It covers
wins, losses, fastest-lap combinations, ties, unset holders, incomplete races,
damage and key packets, championship completion, reset and forfeits. Separate
native sequences run four complete championships per configuration and check
MP save continuation.

The recorded state run completed 7,056 original comparisons, 3,699,374,784 RAM bytes
and 10,560,826 original instructions with zero mismatches. This includes 1,680
race-exit branches and 560 control packets. Native sequences complete 224
championships containing 2,240 synthetic races, with 168 MP save roundtrips.

Drawing calls are excluded from the state-only matchup and bonus comparisons;
their layout/font scratch bytes are explicitly excluded. The separate screen
oracles verify the original drawing; session checks exercise the complete page
flow. Generated evidence
and binaries remain under `analysis/multiplayer-state/`.

The payload is 408,540 bytes loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
REA dossiers are retained under `analysis/rea/multiplayer-*.json`; MC68000
disassembly independently confirms the routine operands and branch order.

## Result screens

`makeNativeMultiplayerImage` renders the original local championship screens for
two through eight drivers. The page enum distinguishes the race matchup, record
message, individual race result, season standings, track time table and track
bonus result. The renderer reads the prepared player IDs, names, scores and BCD
times without changing game state.

The screens retain the original menu artwork, palettes, seven-pixel text and
six-pixel portrait names. A local driver's portrait uses the original face,
per-driver colour mask and centred name. The other car uses the original assigned
face. Race numbering includes both attempts for each local driver, and Super
League track labels retain their original spacing and suffix.

The time tables and bonus screens follow `displayLeagueRaceResults` at
`0x5e93e`. Their row positions change with the driver count. Track bonus results
show the race-time and best-lap record holders above the sorted table; six- and
seven-driver layouts retain the original four-pixel displacement for the table
heading. All table rows use the original four-pixel text displacement. The
eight-driver layout uses its separate final-row adjustment. A missing time uses
the original seven dashes.

Sorting, once-per-track bonus awards and championship progression belong to the
state routines. Rendering does not repeat them. The optional entry text offset
allows isolated checks of the original caller's presentation state. Competitive
page transitions enter at offset zero; `applyNativeMultiplayerScreenState`
records the offset left by drawing, and the original fire wait clears it when
the page is dismissed.

`native_multiplayer_render_tests` executes the untouched 68000 drawing routines
and compares every screen pixel across all player counts, divisions, contexts,
variable names, result flags, BCD times and final-season headers. Generated
screens and evidence remain under `analysis/multiplayer-render/`.

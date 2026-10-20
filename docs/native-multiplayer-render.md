# Native multiplayer championship screens

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

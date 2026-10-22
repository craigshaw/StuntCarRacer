# Native opponent car

The other car uses the original procedural screen-space construction, recovered
from the supplied disk's loaded payload. It is not an imported replacement mesh.
The three opponent wheel heights produce four footprint corners; road support
heights clamp a second footprint used for the shadow. Four transformed road
positions project those two sets of heights through the game's angular tables.
The resulting pairs determine the body, four wheels and shadow with the original
word arithmetic, size clamps, rounding and one-pixel biases.

`src/opponent_car.cpp` implements original entry `0x67aa6` and its model builder
`0x599e2`. `OpponentCarInputs` holds the four footprint heights in original
X1,Y1,X2,Y2 order, their four already camera-relative X/Z coordinate pairs,
three road support bounds, angular camera state and visibility flags. The
adapter in `src/state/opponent_car_adapter.h` documents the original RAM format;
the algorithm itself has no RAM, executable-code or emulator dependency.

The frame returns 36 clipped edges and up to ten ordered quadrilateral fills.
Their original palette indices are shadow5, wheels0, body12, top10, front15
and rear9. Original distance and ahead guards remain intact. The shadow has
its original road-offset and curve-smoothing guards. The shared line-buffer
budget is also preserved, rather than assuming that car drawing always has
unlimited scratch space.

The road painter admits the car once at its original ahead, behind or
occluded slot. It uses the opponent's queue marker and the signed relative
position flag, so road and barrier fills can cover the appropriate parts of
the car. `consumeOpponentCarPainterSlot` implements original entries
`0x6916c`, `0x6917c` and `0x69188`. This selection is separate from generating
the model: a rejected distance guard still consumes a matching queue marker,
as in the original.

Competitive rendering also recovers the original far-track queue, rather than
trying to insert a distant car into the three dense near-track segments.
`src/opponent_far_track.cpp` implements cached-section projection `0x65ec4`,
queue construction `0x67460` and far painter `0x690bc`. It starts with the
endpoint carry and unrolled side coordinates left by the dense passes. The
cache stream and per-segment projection limits come from the local asset pack.
Original lower-edge silhouette admission, angular stopping rules, cache wrap,
queue limit and opponent marker selection are preserved.

The far and near queues, horizon, mountains and car share the original
line-buffer budget. Road edges are allocated first, then the horizon's forced
line and each selected mountain template's edges. The forced horizon path
trims raster storage at horizontal viewport intersections; ordinary road and
mountain lines retain their outside-viewport padding. Near painter fills may
then allocate viewport boundary lines before reaching the car's slot. The car
is therefore constructed lazily at its admitted slot, using the allocation
counter at that point. Its allocations remain visible to later painter fills.

Far painter commands run before the near painter, sharing one mutable opponent
marker; a car drawn in the far pass cannot be drawn again by the near pass.
Intervening road sections can cover the car on hills in the original painter
order. The far and background projection arrays are retained as presentation
history for the next tick. When a grid lookup fails, the presentation restores
the zero render index left by the original previous frame; this uses the
existing native lookup result and never writes into verified gameplay state.

All projection tables come from the user's ignored local asset pack. No original
model, pixel, palette or table data is checked into source control. Procedural
shape dimensions and material indices are recovered instruction operands.

## Evidence and verification

The payload is `analysis/reference/vesuri/offline-extracted-payload.bin`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`,
mapped at `0xe700`; its supplied ADF identity and extraction are recorded in
`analysis/map/manifest.json`. Focused retained REA evidence is under
`analysis/rea/opponent-*.json`:

| Boundary | Evidence ID |
| --- | --- |
| Car guard and ordered material commands | `ev_d5fad36ad9999a1f421943e86c8e4118cd7b5ad5e1ae52e41b438577ef814265` |
| Clamped footprint and procedural construction | `ev_3a91552c0ea78d5c1e70ab3ce25ab673429fa75962aa9f4e5b952aaee74b575f` |
| Horizontal projection and special perspective path | `ev_b9e2dee645c12a6a61efd6d27498cc2f19d145b1390e0d4438f74407a275e9b0` |
| Height projection | `ev_9ed11e3532679e338ce845956b1f48bcb6ffd1e82ccd7a47a027c673abf27586` |
| Angular division | `ev_835e20b1e60886d61b92a772bb75aad7ce50845d1e30c994d6973922982bbc1e` |
| Road painter admission sites | `ev_375a34a49ce32ffc6ca5ce1bf829f93c0478555cd79f9576f0dbaba2f402bdc5` |
| Far cache projection | `ev_0a2f382c36a2f3aad02034a2e3206f52a10557f48cc018295aa72958eeb76b53` |
| Far edge queue and opponent marker | `ev_678b055f371b4bde521dbd32f329af9fcaf79718a360560bca13cc0846c53297` |
| Far painter ordering | `ev_302e8875a57cf300d3c58622f42605593774438836463513bc73b40c74b36ef0` |
| Far left/right panel materials | `ev_b1823203702f420162f4c195344c795d3c11a156be307b70fd4ec166d79f008a`, `ev_1acc2d58b18fcc587a6a187bb2991cde0d61ed92e90e79a1b1ec3c6208ffc132` |

These static dossiers are corroborated by the local Musashi MC68000 oracle,
which executes the original unmodified projection, model construction,
clipping and command selection. It substitutes only the final planar
quadrilateral raster call with command capture. Native and original source
points, model vertices, edge clipping metadata, ordered material boundaries
and line allocation are compared. Separate executions exercise all three
painter entries and their queue-marker writes.

The deterministic model oracle passes 12,040 cases, comparing 432,432 clipped
edges and 9,941 ordered material commands, plus 450 painter-slot cases:
50,294,441 original instructions with zero mismatches. Cases include distance
and ahead guards, visible/offscreen/behind views, pitch/yaw/roll, varying wheel
and support heights, both perspective branches, shadow guards and line-buffer
exhaustion. These model cases are synthetic calling-boundary tests, separate
from the full driving replay comparisons.

The integrated near-painter oracle passes 8,100 synthetic queue cases,
129,330 ordered slot callbacks and 6,187 one-time car admissions, including
12,981 calls during empty command phases, with zero mismatches. It executes
the original road/barrier painter and captures only final raster leaves and
the separately verified car routine's call boundary.

The far-track oracle passes 384 frames constructed from native race starts
across all eight tracks, varied restart segments and four camera quarter turns.
It compares 7,350 clipped edges and 17,544 ordered commands across 4,608 painter
variants, including 1,044 far-pass car admissions. It additionally executes
384 actual original horizon/mountain rendering passes to verify their shared
arena allocations: 9,498,035 original instructions with zero mismatches.
In 86 tested frames the far queue adds an
opponent marker which the three dense segments did not contain. These are
synthetic opponent placements used to test admission, rather than a claim that
all 86 placements satisfy the car's independent distance guard.

The final scene oracle runs 4,096 complete original main-loop updates from
fresh native competitive seeds across all eight tracks, alongside an
independently evolving `ReplayScene`. It executes every original renderer and
raster call, retaining the original CPU and RAM across ticks. All 4,096 car
command frames match, including 2,056 actual car calls and their precise arena
allocations, 51,400 car input values and 6,270 ordered car polygons:
414,126,940 original instructions with zero mismatches. It also checks the
dense queue's allocation and length before far rendering. Its original-only
fixture initialization executes the untouched mountain loader at `0x696fc`,
because native presentation reads the same placements directly from its
decoded assets rather than serializing that original rendering table into RAM.
No original outputs seed or correct the native scene after startup.

The native backend retains the project's existing polygon coverage rules.
Command-level agreement establishes the original shape, projection, materials
and painter placement; it does not claim pixel identity for the backend's final
polygon rasterization. Optional display interpolation affects presentation
inputs and never changes the fixed-rate opponent simulation.

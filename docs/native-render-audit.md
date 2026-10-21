# Native rendering audit

Run the bounded audit with:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_render_audit.py
```

The wrapper builds the native audit and independent original-code geometry
comparators. It records source, executable and local asset-pack hashes, rejects
stale builds, and writes results and images under `analysis/render-audit/`.

## Evidence and checks

The horizon check executes the original projection at `0x67888` through
`0x678d0`. It covers both signed states of the original `0x1bb68` mode byte,
several pitch angles and several rolls. A nonnegative mode lowers the horizon
by eight angle units; a negative mode retains its original height. The native
sky/ground mask is checked against those projected endpoints. This checks the
geometric half-plane and native coverage; it is not a custom-chip raster capture.

Native coverage checks use adjacent clipped quadrilaterals and diagonal triangle
pairs in both windings. They require a filled rectangle without seams and keep
world drawing within the original 256-by-128 viewport. The image-fitting checks
cover the original source dimensions, hypothetical wider sources and several
host window shapes. These are presentation consistency checks.

Thirty-two scenes cover all eight tracks, Standard and Super League, and both
Practice and competitive modes. Authored digital controls exercise off-track
motion and original restarts. Draw Bridge scenarios start near the bridge using
the recovered restart initializer. They retain each tick's moving geometry.
These placements are synthetic verification scenarios, rather than completed
lap claims.

Each rendered scene has a headless twin. Default views, interpolation, alternate
camera offsets, cockpit visibility and reverse queries of the live scene's last
two frames must preserve saved frames, bridge snapshots, later frames and the
complete simulation memory at race exit. Angular offsets must visibly change
the image with the cockpit still visible; hiding it is checked separately.
Returning to default settings must restore the baseline image, including after
an alternate view invalidates its cache. The separate recorded-lap rendering
check covers seeking through the complete retained replay.

The existing far-track, near-track and mountain oracles independently compare
original selection, projection, clipping metadata, command ordering and material
choices. Their reports retain their own test seams and scope declarations.
Matching commands do not establish matching original pixel coverage.

The shared scene-command oracle also executes the original full Practice
renderer caller with its persistent vertex history. The fresh off-track
trajectory exercises two previously missing parts of the near-track test
adapter: caller marker initialization at `0x64fba..0x64fe2`, and the extreme
horizontal-angle rejection helper at `0x6847c..0x684a6`. The adapter executes
these original instructions and checks their bytes against the payload; it
does not seed its expected marker from the native result.

## Visual comparisons

Practice samples reconstruct the earlier dense-near-plus-generic-mesh native
path alongside the recovered far/near painter, using the same immutable camera,
track, mountains and palette. Both comparisons use the corrected horizon mode
so the geometry change is measured separately. Per-snapshot changed-pixel
counts are recorded in `native-comparison.csv`. The largest difference in each
scene is exported as `before-native-mesh.bmp`, `after-native-painter.bmp` and a
yellow difference mask.

The baseline audit observes changed pixels in 255 of 400 Practice samples.
The largest difference is track 4 in the Super context at tick 336: 4,858 of
32,768 world pixels. The earlier mesh path overlays large vertical wall strips
across the sky and road; the recovered painter preserves the visible road and
side silhouette. The isolated horizon check separately verifies the original
eight-unit mode-dependent shift.

Those images compare two native presentations. They are not original Amiga
framebuffer references. The audit does not claim universal pixel equality for
the world renderer or emulate the original blitter. The faithful baseline keeps
original geometry, visibility decisions and painter order, while scan conversion
and host image presentation remain explicit native responsibilities.

The view API operates on private presentation copies. It provides a foundation
for future camera and source-aspect options; this audit does not introduce a
widescreen field-of-view mode or a launcher setting.

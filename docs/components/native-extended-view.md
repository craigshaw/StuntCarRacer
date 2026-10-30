# Original, Extended, 16:9 and 32:9 cockpit views

The launcher and in-game Settings offer **Aspect ratio → 4:3 (Original)**
(default), **4:3 (Extended)**, **16:9** and **32:9**. Both 4:3 choices retain the original
320×200 bitmap, cockpit geometry, engine, wheels, exhausts, instruments and effects.
Extended exposes scenery in the otherwise unused black corners outside the
upper arch. 16:9 uses the approved straight-brace frame around the same-size
original car and main arch. It adds no tyre, engine or exhaust artwork.
32:9 extends the same derived frame and world farther sideways without new
artwork or further changes to the original car or arch.

## Fixed artwork over a larger world

Original retains the 256×128 world viewport at (32,16). Extended draws the
world behind the artwork over (0,0)–(319,143). Its projected coordinates are
still centred at the original (160,80), with unchanged angular scale, camera
pose and horizon. It reveals more world without widening the arch, moving the
car, cropping the source bitmap or changing its simulation camera.

`makeFrameExteriorMask` flood-fills palette-index-zero regions from the two
upper outside corners. Admission stays within the 32 outer columns and above
row 144, except that the top eight rows also admit connected exterior pixels
beyond those columns. Damage drawing respects this mask; enclosed black roof
and damage details remain opaque. The mask is derived once from the indexed
background, before any RGB palette fade. Interior black details, tyres and
instrument backgrounds remain opaque. This is not a global black colour key.

After world rendering, `composeCockpitFrame` restores every original static
foreground pixel outside the old world viewport except that exterior mask.
Extended fills the one-pixel post/brace notch at (16,83) and (303,83) with the
adjacent dark-red outline colour. These are its only foreground pixel edits;
the source bitmap and Original rendering remain unchanged.
The original software car objects, flames, chains, instrument strip, hardware
wheel sprites and messages then use their existing coordinates and draw order.
The original wheel cuts remain hidden against their original frame.

Near/far admission and edge clipping use bounds [-32,288] × [-16,128] in the
original viewport's coordinate domain. Queue closure, cut-edge rays, mountain
selection, opponent admission and raster bounds use the same expanded domain.
All settings are private presentation inputs, defaulting to original bounds
for recovered APIs and original-code comparisons. View queries rebuild from
private geometry and the rendered tick's bridge snapshot. Physics, RNG, race
state and the 120 ms / 50 Hz / 60 Hz clocks are unchanged.

## Widescreen composition

16:9 renders the world behind a 432×200 indexed foreground. The original car
and main arch remain centred at offset (56,0), with unchanged projection scale;
the original projected centre (160,80) becomes (216,80). The world fills the
whole working canvas, including the exposed lower outer corners. Private
geometry, opponent admission, horizon accounting, viewport closure and raster
bounds use [-88,344] × [-16,184] in the original viewport coordinate domain.

`makeWidescreenFrame` derives only frame and blue interior from the
original background. The outer 14 columns of each side brace are reworked to
join its extension along one straight axis. The original highlight determines
the angle; column 14 supplies a constant, slimmer 33-row material profile.
All main arch and car shapes remain intact. The foreground stores palette
indices and an explicit transparent value of 255. Inner lower black artwork
remains opaque; the upper frame and outer braces receive the translucency below.
No generated image file is loaded at runtime.

`RaceScene::draw` composes that frame after rendering the world.
It draws particles, chains, software car objects, flames, instruments,
hardware wheels and messages onto a separate original-size 320×200 layer,
then composites it at (56,0). Native object positions, animation phases and
clipping stay unchanged. Pause/end panels use the same centred composition.

Both wide modes use a translucent upper cage, matching the earlier upper-frame
study. Opacity is 25% through source row 80, then rises smoothly to 100% by row
100. This ends above the earliest legal wheel crop at row 101. The original
inner posts, blue side panels, engine, wheels and instruments stay opaque.
Only the cage's actual pixels blend; the open windscreen receives no tint.

Outer diagonal braces use 65% opacity. It rises smoothly to 100% across original
columns 0–14 (mirrored on the right), leaving the inner tyre guards solid.
The upper transition takes precedence where it is more transparent. Original
indexed geometry identifies brace material; palette 7/8 side panels keep full
opacity. Frame geometry, pixel scale and the car assembly are unchanged.

The original frame, corner objects and roof damage strip form one private cage
layer. It is blended once over the rendered world, preserving damage marks
without compounding opacity. Particles, chains and the original car layers then
use their normal opaque compositors. Original and Extended 4:3 keep their
existing composition and pixel output.

Temporary race fade captures use 16-bit entries: ordinary palette indices
0–15, or `tag | (frameIndex << 4) | worldIndex` for translucent frame samples.
The tag is `0x100` for upper-only opacity and `0x200` for the side-brace fade.
`presentationColor` resolves both sources against the current fading
palette and the sample's row/column-dependent opacity. This preserves exact live
blends, duplicate palette identities and the black fade endpoint. The codes
never enter saved career images, which continue to capture Original.

32:9 uses an 864×200 canvas with the original car/arch at (272,0). The same
highlight fit and material profile extend its side braces and blue interior;
the central 432 columns exactly match the existing 16:9 frame. Farther out,
the diagonal braces naturally leave the bottom of the canvas, exposing more
scenery. All world consumers use the wider private bounds [-304,560] × [-16,184],
including near/far edges, mountain admission, opponents and raster coverage.
The simulation projection scale and all original-size compositors stay intact.

Expanded views also use larger private road buffers. The original far scratch
area admits only 29 new cross-sections, which can leave visible track cut off
inside a wide canvas. Expanded projection can continue through one complete
cache circuit, stopping earlier at the viewport edge or an authored shorter
projection limit. Its queue grows with that cache; dense storage accommodates
all three near passes. The shared line budget scales with viewport area and
stays within its word counter. Original retains its 29-section, 48-slot and
9,000-byte limits, and its original presentation history layout.

## Display and preferences

The 320×200 bitmap is presented at 4:3 with the original non-square pixel
proportions (pixel width:height 5:6). This correction happens at the host; it
does not resample stored game artwork or alter projection calculations.
Integer Auto/fixed scales use 320×240 display-size steps and nearest-neighbour
sampling; 3× produces 960×720 for 4:3. Widescreen uses the same car pixel
proportions. A symmetric outer crop presents its 432-column working canvas
through a 16:9 aperture: at 3×, 1296×720 is cropped by eight display pixels per
outer side to 1280×720. The complete original car remains 960×720 centrally.
Only added outer pixels are cropped. Fixed widescreen 1×/2×/3× client sizes
are 427×240, 853×480 and 1280×720, with rounded widths at the first two scales.
Fit fills the largest rectangle at the selected aspect ratio. Stretch fills
both target axes explicitly. Filtering and smoothing remain independent.

32:9 uses the same 81/80 working-canvas overdraw and symmetric outer crop. At
3×, its 2592×720 canvas presents through a 2560×720 aperture, cropping sixteen
display pixels from each added outer edge. Its 1×/2×/3× client sizes are
853×240, 1707×480 and 2560×720. The centred original car still spans 960×720 at
3×, matching 4:3 and 16:9 at the same height.

Apply saves the selection; Cancel restores it and window placement. Version 8
preferences store `aspect=original|extended|widescreen|ultrawide`. Versions 6 and 7
retain their existing choices. Versions 1–4 select Original;
the superseded version-5 `view=widescreen` prototype migrates to Extended.
Menus and track previews retain their original bitmap and 4:3 proportions.
Race fades use the selected view and its canvas dimensions. Career finish
screenshots explicitly render Original and retain their existing 320×200 codec.

## Verification

`native_extended_view_tests` checks both corner components and rejects a global
black key. It verifies fixed foreground pixels across 432 software wheel,
flame, hardware sprite and suspension combinations, the two seam repairs, and
roof transparency with both black and red damage-strip pixels. Eight-track runs include
exact/interpolated queries, repeatability, switching back to Original, pause
cleanup, palette capture and full terminal memory equality against a twin.
The same assembly variants and eight-track queries also cover 16:9, including
the full car and main arch, lower-corner scenery, end/pause panels and fades.
They also cover 32:9, its additional outer scenery and unchanged central frame.
The wide checks protect all original car pixels and the opaque lower frame;
translucent cage pixels are checked separately for blend/fade equivalence, intact blue
panels, unchanged open scenery, opacity rounding and the fully opaque boundary.
Complete scenes check the captured upper-frame palette pairs against live RGB.
Game-session checks render entry and exit fades in both wide modes.
Side-brace checks also cover mirror symmetry, the 65% blend and the solid joins
before the original tyre guards.

`native_track_visibility_tests` reproduces the Stepping Stones Practice drop
at three ticks, requiring the red/white wall to reach every outer-right column
with and without interpolation. It checks expanded coordinate/queue capacity,
allocation beyond the original line ceiling, bounded cache traversal, authored
shorter cuts and retained Original limits. It exports the three fixed frames.

The 32-scenario rendering audit covers both leagues, Practice and competition,
with original-code comparisons and independent snapshot/RNG checks. Display
tests cover all four aspect choices, nearest/bilinear scaling, fixed window
sizes, preference migration and cancelling. Real Windows display captures
verify unchanged car scale and clipping at both wide apertures. A career fixture
finishes with 32:9 active and retains the original finish-image codec.
Captures and source/executable-identified
receipts belong under ignored analysis/ or CTest output. These are bounded
checks, not a claim of exhaustive visibility or original world-pixel identity.

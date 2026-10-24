# Original, Extended and 16:9 cockpit views

The launcher and in-game Settings offer **Aspect ratio → 4:3 (Original)**
(default), **4:3 (Extended)** and **16:9**. Both 4:3 choices retain the original
320×200 bitmap, cockpit geometry, engine, wheels, exhausts, instruments and effects.
Extended exposes scenery in the otherwise unused black corners outside the
upper arch. 16:9 uses the approved straight-brace frame around the same-size
original car and main arch. It adds no tyre, engine or exhaust artwork.

## Fixed artwork over a larger world

Original retains the 256×128 world viewport at (32,16). Extended draws the
world behind the artwork over (0,0)–(319,143). Its projected coordinates are
still centred at the original (160,80), with unchanged angular scale, camera
pose and horizon. It reveals more world without widening the arch, moving the
car, cropping the source bitmap or changing its simulation camera.

`makeReplayExteriorMask` flood-fills palette-index-zero regions from the two
upper outside corners. Admission stays within the 32 outer columns and above
row 144, except that the top eight rows also admit connected exterior pixels
beyond those columns. Damage drawing respects this mask; enclosed black roof
and damage details remain opaque. The mask is derived once from the indexed
background, before any RGB palette fade. Interior black details, tyres and
instrument backgrounds remain opaque. This is not a global black colour key.

After world rendering, `composeReplayFrame` restores every original static
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

`makeReplayWidescreenFrame` derives only frame and blue interior from the
original background. The outer 14 columns of each side brace are reworked to
join its extension along one straight axis. The original highlight determines
the angle; column 14 supplies a constant, slimmer 33-row material profile.
All main arch and car pixels remain intact. The foreground stores palette
indices and an explicit transparent value of 255, so palette fades work and
black artwork remains opaque. No generated image file is loaded at runtime.

`ReplayScene::draw` restores that static foreground after rendering the world.
It draws particles, chains, software car objects, flames, instruments,
hardware wheels and messages onto a separate original-size 320×200 layer,
then composites it at (56,0). Native object positions, animation phases and
clipping stay unchanged. Pause/end panels use the same centred composition.

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

Apply saves the selection; Cancel restores it and window placement. Version 7
preferences store `aspect=original|extended|widescreen`. Version 6 retains its
existing choice. Versions 1–4 select Original;
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

The 32-scenario rendering audit covers both leagues, Practice and competition,
with original-code comparisons and independent snapshot/RNG checks. Display
tests cover all three aspect choices, nearest/bilinear scaling, fixed window
sizes, preference migration and cancelling. Real Windows display captures
verify unchanged car scale and clipping at the 16:9 aperture. A career fixture
finishes with widescreen active and retains the original finish-image codec.
Captures and source/executable-identified
receipts belong under ignored analysis/ or CTest output. These are bounded
checks, not a claim of exhaustive visibility or original world-pixel identity.

## 16:9 artwork study

The development-only `native_cockpit_study` target reproduces the approved
artwork reference. Run it with the asset-pack path and an
output directory. It centres the original 320×200 car and main arch on a
432×200 working canvas, extending only the diagonal side braces and blue
interior. The outer 14 columns of each original brace are also reworked to
join the extensions along a straight axis. The braces use their slimmer
column-14 profile and original pale highlight's fitted direction, keeping
their width and shading bands constant.
BGRA outputs retain real transparency for future world rendering. The study
checks exact protected car/arch pixels and symmetric extensions; its contextual
image retains the protected native scene and leaves new scenery areas transparent.
Display previews use the existing pixel proportions and a symmetric outer
crop to present 16:9 without changing the original cockpit's displayed size.
Neither 4:3 option nor the player executable is changed by this tool.

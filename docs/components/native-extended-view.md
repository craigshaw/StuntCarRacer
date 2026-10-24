# Original and Extended cockpit views

The launcher and in-game Settings offer **Aspect ratio → 4:3 (Original)**
(default) and **4:3 (Extended)**. Both retain the complete original 320×200
bitmap, cockpit geometry, engine, wheels, exhausts, instruments and effects.
Extended exposes scenery in the otherwise unused black corners outside the
upper arch. It uses no new or generated car artwork. 16:9 is deferred until
this composition has been reviewed; it is not a selectable unfinished mode.

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

## Display and preferences

The 320×200 bitmap is presented at 4:3 with the original non-square pixel
proportions (pixel width:height 5:6). This correction happens at the host; it
does not resample stored game artwork or alter projection calculations.
Integer Auto/fixed scales use 320×240 display-size steps and nearest-neighbour
sampling; 3× produces 960×720. Fit fills the largest 4:3 rectangle. Stretch
explicitly fills both target axes. Filtering and smoothing remain independent.

Apply saves the selection; Cancel restores it and window placement. Version 6
preferences store `aspect=original|extended`. Versions 1–4 select Original;
the superseded version-5 `view=widescreen` prototype migrates to Extended.
Menus and track previews retain their original bitmap and 4:3 proportions.
Race fades use the selected view. Career finish screenshots retain their
existing 320×200 codec.

## Verification

`native_extended_view_tests` checks both corner components and rejects a global
black key. It verifies fixed foreground pixels across 432 software wheel,
flame, hardware sprite and suspension combinations, the two seam repairs, and
roof transparency with both black and red damage-strip pixels. Eight-track runs include
exact/interpolated queries, repeatability, switching back to Original, pause
cleanup, palette capture and full terminal memory equality against a twin.

The 32-scenario rendering audit covers both leagues, Practice and competition,
with original-code comparisons and independent snapshot/RNG checks. Display
tests cover 4:3 geometry, nearest/bilinear scaling, fixed window sizes,
preference migration and cancelling. Captures and source/executable-identified
receipts belong under ignored analysis/ or CTest output. These are bounded
checks, not a claim of exhaustive visibility or original world-pixel identity.

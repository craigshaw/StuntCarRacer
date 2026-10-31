# Rendering and audio

[app/presentation/](../../app/presentation/) and
[src/render/](../../src/render/) own drawing;
[app/race/race_scene.cpp](../../app/race/race_scene.cpp) captures the race frames.
Player controls and display choices are documented in [playing](../playing.md).

## Snapshots

Live racing and replay share `RaceScene`. Retained frames own camera, cockpit,
events, moving-track data and geometry history. `renderView` and interpolation
use private copies: repeated, reverse or alternate-camera queries must leave
simulation, RNG, saved frames and future history unchanged.

Smoothing blends completed poses, adding one gameplay tick of live presentation
latency. Restart and terminal transitions cut rather than blend. Particle births,
recycled slots, image phases, damage and HUD events remain discrete; wheel
rotation follows its separate 50 Hz timeline.

The shared scene builder emits far commands before near commands in both Practice
and racing. Road, horizon, mountains and the lazily inserted opponent share line
allocation and painter order. Preserve that history and the opponent marker;
generic depth sorting is not a substitute for the recovered command stream.

The optional [ghost renderer](personal-best-ghosts.md) carries reciprocal forward
depth with projected road/side endpoints through clipping and viewport closure.
When requested, the same ordered scanline fills write colour and interpolated
depth over identical spans; later painter fills replace both. This presentation
metadata is not written to recovered RAM. Ghost-disabled rendering keeps its baseline
colour path and allocation limits; a separate mesh rasterizer is not the ghost's
live track-visibility mask.
Viewport closure may use a clamped endpoint of a rejected line; its depth
interpolation stays within that source edge rather than extrapolating through
zero. Missing endpoint attributes remain unknown and fail the proof's complete
coverage check instead of borrowing an arbitrary depth from another edge.

## Views

Original and Extended use a 320×200 canvas. The 16:9 and 32:9 canvases are
432×200 and 864×200, with the original car centred at unchanged scale. Host
presentation corrects the original pixel proportions; wide apertures crop only
added outer pixels. Menus, previews and saved finish images remain Original 4:3.

Expanded views must update projection admission, clipping, queue storage,
mountains, opponents and raster bounds together. Their larger buffers and line
budgets are private and bounded; Original retains its recovered limits.

Derive exterior transparency from indexed artwork topology, before palette fades;
a global black colour key would erase opaque car details. Compose the wide cage
and its damage once before blending, then draw the opaque car/effect layers.
Fade captures retain both frame and world palette identities for blended pixels.

## Audio and host state

[src/audio/](../../src/audio/) preserves ordered sample requests, 50 Hz engine
updates and sample-completion events. Audio consumes copied inputs and separate
state; the Windows output worker never writes gameplay state. Pause stops audio
advancement as well as gameplay.

Overlays and focus/controller loss preserve the fractional gameplay clock and
suppress held input until release. Settings Cancel restores window placement
and preferences. Preserve preference migration when adding settings; filtering,
aspect and smoothing remain independent choices.

Use [verification by change](../development.md#choose-verification-by-change),
including simulation-isolation checks and visual inspection. Native raster
coverage and audio output do not establish cycle-exact Amiga hardware behaviour.

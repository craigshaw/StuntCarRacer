# Simulation and fidelity

Typed algorithms live in [src/physics/](../../src/physics/),
[src/race/](../../src/race/) and [src/track/](../../src/track/).
[src/state/native_race.h](../../src/state/native_race.h) owns their stage order
and recovered-layout adapters. The [architecture](../architecture.md) defines
the independent clocks and fidelity boundary.

## Rules for extensions

- Preserve signed widths, intermediate wraparound, shifts, packed-decimal timing
  and RNG call order. Equivalent-looking arithmetic or a reordered stage can
  change later races.
- Keep fixed gameplay steps during stalls. Host frame rate, interpolation,
  overlays and audio callbacks cannot advance gameplay or consume its RNG.
- Treat restart, game reset, track preparation and race exit as separate
  operations. They retain different state and have different RNG effects.
- Compare gameplay changes with the supported disk's original execution.
  The enhanced Vesuri disassembly differs in timing and scheduling; its labels
  and constants are not an authority for this disk.

## Stage boundaries

The damage routine reads occupancy from the original planar damage strip.
`initializeNativeDamageHoles` must restore that strip before warm-up; replacing
only the visible RGB image changes future damage progression.

Draw Bridge updates after frame/wheel capture and before damage. Its new shape
feeds the next driving frame. Refresh physics track data after an update while
retaining the earlier geometry in that frame's snapshot. Preview initializes
the bridge once and rotates that fixed pose without advancing its phase.

Cockpit, particle and message draws retain events at their original stage.
Later timing code can clear clocks, replace messages or recycle particle slots;
reconstructing earlier draws from final memory loses that information. Keep
warm-up and restart events as well as ordinary race events.

Use the [verification guide](../development.md#choose-verification-by-change)
to select checks. Original/native sides must evolve independently from the
same seed and controls; original outputs must never reseed native state.

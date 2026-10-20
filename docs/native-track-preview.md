# Native Practice track preview

The Practice route renders the original overview natively from the local ADF
asset pack. `NativeTrackPreview` owns a copy of initialized track memory. Its
projection and drawing scratch never becomes part of the subsequent race.

The entry corresponds to original `runTrackPreviewScreen` at `0x5d20c`, with
`renderTrackPreview` at `0x604b4` and input at `0x5d980`. The encoded background
and sixteen palette words are read at `0x2b994` and `0x2b974`; the original
patched seven-pixel font, track-name table and league text supply all text.
The supplied disk's instruction text is retained, including its release-specific
wording. No image, font, text or track arrays are embedded in source control.

The overview uses the recovered dense track geometry. Native code traverses the
original 17-by-17 grid in camera-dependent painter order, uses the original four
camera choices, and projects each segment through the existing word-exact
preview branch in `projectNearTrackSegment`. It builds the original road and
barrier command sequence, with the preview's side-colour override. It does not
use the replay viewer's coarse mesh or its depth buffer. Its integer edge
accumulator, horizontal-edge rules and nested active-edge polygon filler are
native translations of the original raster routines. A generic triangle or
even/odd filler produces small differences at the original banked corners.

Left rotates the overview forward by one quarter turn; right rotates backward.
Left takes precedence if both are held. The original release-mask handshake
prevents the menu's held fire button from immediately starting a race and
prevents a held direction from repeatedly rotating. Fire begins the race after
release. Up and down have no preview action.

`tests/native_track_preview_tests.cpp` runs the original payload through a
verification-only 68000 core and compares the entire 320-by-200 display against
the native overview. The original payload is never patched. The harness models
the display interrupt countdowns to let the original palette fades complete;
no CPU is linked into the native game. The check covers seven admitted tracks
from all four camera angles, plus repeated left/right rotations using the
original's retained scratch: **84 complete images, 5,376,000 indexed pixels,
zero mismatches**. A further **448 original-input comparisons** cover held
buttons, release gating, left/right priority, fire, and byte-angle wrapping.
Input comparisons supply the digital mask at the already verified controller
decoder's boundary; they execute the preview input instructions themselves.

The track decoder contains all eight tracks. Track 5's original overview also
calls the moving drawbridge animation initializer before generating geometry;
using its static decoded data is visibly different. `load` explicitly rejects
that track, matching the existing native race restriction. The menu handles the
boundary before entering the preview. Recovery of that original animation is
required before admitting its overview and race together.

Static evidence: REA `ev_5834bf2b6a0ae77d1953aad0fd60c9b3cfef3f649278e9075e1adef13cce0c29`
describes the original screen entry and dependency calls. Full original-code
execution and indexed framebuffer comparisons corroborate the visible result.
The named upstream disassembly is a reading aid; original addresses, operands
and runtime pixel output take precedence over its enhanced scheduling and
provisional data labels. For example, the table at `0x6129a`, provisionally
called `trackDisplayYOffsets`, actually patches the title's column control byte
at `0x643ad`, centering track names of different lengths.

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
use the driving scene's far/near painter. Its integer edge
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
no CPU is linked into the native game. The check covers all eight tracks from
all four camera angles, plus repeated left/right rotations using the original's
retained scratch and the stationary bridge wait described below: **97 complete
images, 6,208,000 indexed pixels, zero mismatches**. A further **448
original-input comparisons** cover held
buttons, release gating, left/right priority, fire, and byte-angle wrapping.
Input comparisons supply the digital mask at the already verified controller
decoder's boundary; they execute the preview input instructions themselves.

Track 5 calls the recovered Draw Bridge routine once before generating its
geometry. `load` repeats the original fresh player configuration and track
decode inside its owned copy, advances the original bridge initializer, then
rebuilds the dense cache. Its source memory stays unchanged. This also prevents
an already animated caller, repeated load, or quarter turn from advancing the
overview's bridge a second time.

The original overview does **not** animate continuously. At `0x5d2da` it calls
`animateDrawBridge` (`0x5a794`), then generates its render cache at `0x5d2e6`
and draws the overview at `0x5d2ec`. The loop at `0x5d2fc..0x5d30c` waits for
input, redraws only when rotating, and returns directly to waiting. It never
calls the bridge routine again. The bridge starts in the original raised
position and holds that position throughout the preview. Continuous movement
belongs to the driving loop.

The oracle additionally executes the complete original overview entry and
**1,000 neutral controller polls** in that wait loop. It observes exactly one
bridge call and one render, compares the resulting complete framebuffer, and
checks that the native overview likewise stays unchanged. The input boundary
uses the same normalized decoder result as the input tests; the original menu
and bridge instructions are never patched.

Static evidence: REA `ev_5834bf2b6a0ae77d1953aad0fd60c9b3cfef3f649278e9075e1adef13cce0c29`
describes the original screen entry and dependency calls. Full original-code
execution and indexed framebuffer comparisons corroborate the visible result.
The named upstream disassembly is a reading aid; original addresses, operands
and runtime pixel output take precedence over its enhanced scheduling and
provisional data labels. For example, the table at `0x6129a`, provisionally
called `trackDisplayYOffsets`, actually patches the title's column control byte
at `0x643ad`, centering track names of different lengths.

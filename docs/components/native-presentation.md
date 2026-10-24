# Native presentation boundaries

The simulation keeps the original fixed update rate and verified physics,
collision, AI, RNG and race state. Rendering consumes immutable snapshots.
Faithful appearance is the default; universal world-pixel identity is not a
release condition. Original-code command comparisons and visual inspection
remain the basis for correcting visible defects.

`ReplayScene` now uses one recovered road command path for Practice and racing.
The far painter and its shared horizon/mountain allocation run in both modes;
only opponent insertion depends on race mode. This removes the early Practice
fallback that used coarse road triangles and a depth buffer. The horizon also
uses its original signed mode flag when choosing the vertical offset.

`ReplayScene::renderView` takes independent `ReplayViewSettings`: relative
presentation yaw, pitch and roll, and cockpit visibility. The default settings
retain the existing exact/interpolated view, including its captured camera
matrix phase. Alternate views rebuild from private geometry history. They do
not alter the driver camera, recorded controls, saved frames, moving bridge
snapshots or simulation state. These queries are a developer-facing foundation;
there is no new camera menu or keyboard binding in the game.

`fitPresentationImage` fits a renderer-owned source image into a host surface.
It preserves aspect ratio, uses integer enlargement when possible and reduces
small images to fit rather than cropping. Both Windows hosts use it. Tests
include the original source size, wide host windows and hypothetical wider
source images.

The native game's host now supplies a separate display overlay and borderless
fullscreen, with persisted image sizing, filtering, integer scale and motion
preferences. Integer uses nearest-neighbour samples at Auto or fixed 1×–32×.
Fit preserves the 4:3 display aspect; Stretch fills both target axes. Both expose
independent nearest-neighbour or separable bilinear filtering, with clamped
edges and cached coordinates. Version 6 preferences migrate versions 1–5:
integer becomes Integer Auto, sharp becomes Fit/Nearest neighbour and smooth
becomes Fit/Bilinear. Invalid fields reject the complete preference file.
Windowed fixed scales resize the client in physical pixels, capped to the
monitor work area; fullscreen fixed scales centre without cropping. Cancel
restores the pre-edit window placement as well as preferences. The source image remains
immutable. Overlay navigation suspends game/audio updates and retains the current
physics-clock fraction. See [display controls](../playing.md) for the keyboard,
controller and preference-file behaviour.

The native Game menu opens with Esc in races/the mode chooser, P outside name
editing, or controller Start. Nested menu Esc continues to reach the original
Back handling. Resume and Settings are always offered; Leave Race appears only
for an unfinished race and feeds the original latched quit request. Quit to
Desktop opens a separate confirmation defaulting to Cancel, then uses
`NativeGameSession::requestClose`. A pending save decision remains interactive;
cancelled closing restores the Game menu. Host menu, Settings and confirmation
pages block simulation/audio without consuming the retained race-clock phase.

The optional F-key FPS counter is composed last at the host surface's top left,
after game scaling and Pause/Settings. It draws text directly over the image
without a background panel. Its one-second average counts presented
game frames, independently of simulation ticks. The counter starts hidden and
does not change preferences, game pixels or saved state; name entry retains F.

Original and [Extended](native-extended-view.md) retain 320 by 200, presented
at the original 4:3 display proportions. Original uses its 256 by 128 world
viewport. Extended draws across 320 by 144, then composites the original frame
with only its outside-connected upper black corners transparent. The complete
cockpit and car remain in their original positions. Integer and Fit use the
4:3 display aspect; Stretch changes display proportions while retaining its view.
A complete free camera still needs independent placement and broader visibility
handling. These options remain separate from simulation.

The native line and polygon rasterizer still has its own coverage rules.
The recovered painter fixes geometry ordering and materials; that does not
establish identity with the Amiga's final blitter and sprite pixels. See the
[bounded rendering audit](native-render-audit.md) and
[shared scene commands](native-scene-commands.md) for evidence and limits.

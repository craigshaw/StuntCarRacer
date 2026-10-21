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
fullscreen, with persisted scaling and motion preferences. Integer and sharp-fit
presentation use nearest-neighbour samples; smooth-fit uses a separable bilinear
filter with clamped edges and cached coordinates. The source image remains
immutable. Overlay navigation suspends game/audio updates and retains the current
physics-clock fraction. See [display controls](native-game.md) for the keyboard,
controller and preference-file behaviour.

The current game image remains 320 by 200 and its world viewport remains 256 by
128. Fitting that image into a wide window preserves its original aspect; it
does not extend the field of view. Actual widescreen rendering still needs a
wider projection/culling domain and a decision about cockpit/HUD layout.
Likewise, a complete free camera needs independent camera placement and broader
visibility handling. The new interfaces keep those changes in presentation.

The native line and polygon rasterizer still has its own coverage rules.
The recovered painter fixes geometry ordering and materials; that does not
establish identity with the Amiga's final blitter and sprite pixels. See the
[bounded rendering audit](native-render-audit.md) and
[shared scene commands](native-scene-commands.md) for evidence and limits.

# Native presentation boundaries

The simulation keeps the original fixed update rate and verified physics,
collision, AI, RNG and race state. Rendering consumes immutable snapshots.
Faithful appearance is the default; universal world-pixel identity is not a
release condition. Original-code command comparisons and visual inspection
remain the basis for correcting visible defects.

## Snapshots and views

Optional 60 FPS smoothing blends camera/opponent poses and cockpit placement,
including hardware wheel positions, continuing dust/spark trajectories and
lifting chains. Wheel rotation retains its 50 Hz timeline. Particle births,
recycled slots, dust/spark changes, flame image phases, damage and HUD events
remain discrete. Queries use two completed snapshots, cut across restarts and
terminal actions, and never advance gameplay or presentation animation state.
The smoothing-off path and exact snapshot endpoints retain the original draws.

`RaceScene` uses one recovered road command path for Practice and racing.
The far painter and its shared horizon/mountain allocation run in both modes;
only opponent insertion depends on race mode. This removes the early Practice
fallback that used coarse road triangles and a depth buffer. The horizon also
uses its original signed mode flag when choosing the vertical offset.

`RaceScene::renderView` takes independent `CameraViewSettings`: relative
presentation yaw, pitch and roll, and cockpit visibility. The default settings
retain the existing exact/interpolated view, including its captured camera
matrix phase. Alternate views rebuild from private geometry history. They do
not alter the driver camera, recorded controls, saved frames, moving bridge
snapshots or simulation state. These queries are a developer-facing foundation;
there is no new camera menu or keyboard binding in the game.

## Host settings

`fitPresentationImage` fits a renderer-owned source image into a host surface.
It preserves aspect ratio, uses integer enlargement when possible and reduces
small images to fit rather than cropping. Both Windows hosts use it. Tests
include the original source size, wide host windows and hypothetical wider
source images.

The native game's host supplies a separate display overlay and borderless
fullscreen, with persisted image sizing, filtering, integer scale and motion
preferences. Integer uses nearest-neighbour samples at Auto or fixed 1×–32×.
Fit preserves the selected 4:3, 16:9 or 32:9 display aspect; Stretch fills both
target axes. Both expose independent nearest-neighbour or separable bilinear filtering, with clamped
edges and cached coordinates. Version 8 retains supported versions 1–7: integer
becomes Integer Auto, sharp becomes Fit/Nearest neighbour and smooth becomes
Fit/Bilinear. Versions 1–4 use Original view; the version-5 widescreen prototype
maps to Extended, and versions 6/7 retain their choices. Invalid fields reject
the complete preference file.

Windowed fixed scales resize the client in physical pixels, capped to the
monitor work area; fullscreen fixed scales centre the selected view. Both wide
working canvases overdraw their apertures slightly, cropping only new outer pixels
to retain the original car scale. Cancel restores the pre-edit window placement
as well as preferences. The source image remains immutable. Overlay navigation
suspends game/audio updates and retains the current
physics-clock fraction. See [display controls](../playing.md) for the keyboard,
controller and preference-file behaviour.

## Game menu

The host Game menu is independent of original menu artwork and competition rules.

| Context | Escape | Start / P |
| --- | --- | --- |
| Race, including an unacknowledged finish; mode chooser | Open Game menu | Open Game menu |
| Nested original menu | Existing Back/Cancel route | Open Game menu |
| Player/save-name editor | Existing Back/Cancel route | Start opens Game menu; P types normally |
| Game menu | Resume | Resume |
| Settings | Cancel to caller | Cancel to caller |
| Quit confirmation | Cancel to Game menu | Cancel to Game menu |

B retains Back/Cancel. F10 opens Settings except during Quit confirmation.
Page changes require fresh confirmation/back presses, so held Enter/A cannot
confirm Quit on the next page. Resume restores the underlying screen and clock
fraction; Settings retains preview/Apply/Cancel and window-placement restoration.

Leave Race appears only while unfinished. Its host command selects the normal
quit outcome before another physics tick, including crash/lift/recovery, and
survives post-overlay held-input suppression. Damage, exit and scoring rules
still apply. Controller Back/View and recorded keyboard quits retain the separate
original timing gates.

Quit to Desktop selects Cancel initially. Its confirmation explains pre-race
restart for unfinished career races and loss since the last manual multiplayer
save. Confirmation shares window-close/Alt+F4 and `NativeGameSession::requestClose`:
career/Hall writes flush or offer Retry/Leave Without Saving/Cancel. Cancelling
that decision reopens the Game menu without resuming driving. There is no Return
to Careers or return-to-launcher action; career switching uses its Back hierarchy.

Game menu, Settings and confirmation suspend simulation/audio while presentation
continues, preserving the fractional race clock. Focus loss/disconnection requires
explicit resume. `native_display_tests` and `check_native_game.py` cover routing,
conditional actions, held inputs, cancellation, immediate recovery-state exits,
paused rendering and save-aware closing; device/DPI/layout behaviour also needs
manual inspection. See [persistence](native-career-persistence.md).

## FPS overlay

The optional F-key FPS counter is composed last at the host surface's top left,
after game scaling and Pause/Settings. It draws text directly over the image
without a background panel. Its one-second average counts presented
game frames, independently of simulation ticks. The counter starts hidden and
does not change preferences, game pixels or saved state; name entry retains F.

## Aspect and raster boundaries

[Original, Extended, 16:9 and 32:9 views](native-extended-view.md) retain the
original car artwork and scale; that guide owns viewport, frame/mask, clipping,
opacity and crop details. Sizing/filtering never changes the simulated camera.
Relative-view queries are development APIs, not a complete player free camera.

The native line and polygon rasterizer still has its own coverage rules.
The recovered painter fixes geometry ordering and materials; that does not
establish identity with the Amiga's final blitter and sprite pixels. See the
[bounded rendering audit](native-render-audit.md) and
[shared scene commands](native-scene-commands.md) for evidence and limits.

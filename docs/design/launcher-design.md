# Launcher design

This records the agreed and implemented launcher design.
Project terminology lives in the [glossary](../../GLOSSARY.md).

## Agreed requirements

- Every normal launch opens the launcher. Once assets are ready, Play has initial focus.
- Play enters the original mode chooser, name entry and game menus.
- A player with a built copy and their supported ADF can complete setup and play
  without Python, PowerShell or development tools. The application owns import.
- Import creates a retained local asset pack and preserves the source disk.
  Subsequent launches do not require the ADF to remain present.
- The first version accepts only the currently verified source disk and explains
  unsupported disks clearly. Additional variants require separate verification.
- Configuration covers the existing display mode, scaling, 60 FPS smoothing
  and control presets. The faithful baseline remains available.
- Visual design uses a contemporary layout with restrained Amiga-era colours and
  typography, readable text and clear controls.
- Launcher navigation supports mouse, keyboard and controller. A standard Windows
  file picker provides ADF browsing with normal Windows keyboard/mouse interaction.

## Application lifetime

The launcher and game share one application window. The launcher is a pre-game
screen in this version; in-game Settings remains available after Play. Closing
the game window exits the application. Returning to the launcher with a suspended
game session is outside this version's scope. The launcher always opens windowed;
the selected game display mode takes effect when Play is pressed.

## Player data and settings

Normal player launches retain imported assets, settings and saves in per-user
Windows application data. Development commands can use explicit paths into the
checkout. Ordinary player launches do not create driving recordings; an explicit
development option retains that facility.

Launcher and in-game Settings share one set of saved preferences. Launcher
changes are saved when Play is pressed. The in-game menu retains its existing
Apply/Cancel behaviour. The six visible rows are display mode,
image sizing, Integer scale or Filtering, 60 FPS smoothing (Off/On), the Original/Modern driving preset and
[Responsive/Original menu navigation](menu-navigation-design.md).

60 FPS smoothing controls presentation interpolation; Off retains the original
stepped animation. The host still presents at 60 Hz in either mode, and game
timing is unchanged. Image sizing offers Integer, Fit and Stretch. Integer shows
an Auto/1×–32× scale selector; Fit and Stretch show a separate Nearest neighbour/
Bilinear filter selector. Integer Auto is the default. Fixed scales resize the
windowed game area and centre the fullscreen image, reducing oversized choices
to the largest whole scale that fits. Fit preserves proportions; Stretch fills
both axes. Neither changes the rendered world or field of view. Filter and fixed
scale selections are retained independently across sizing changes.
In-game Settings uses the same labels and saved values.

Closing the launcher discards settings edits not saved through Play, without a
confirmation prompt. Successfully imported assets remain available. Restore
original defaults resets the pending choices to windowed, Integer Auto with nearest-neighbour filtering,
60 FPS smoothing Off, Original driving controls and Original menu navigation;
assets and saved games are unaffected. New/migrated preferences default to
Responsive navigation.

If saving preferences fails, retain the selected values, explain the failure and
offer a retry without starting the game. Missing or invalid preferences retain
the documented defaults, including Responsive navigation. If game initialization fails after
import, keep the launcher accessible with an explanation and retry action.

### Provisional development helper

Initially, Drive Native.cmd will build/check the game and open the new launcher
using explicit paths to the checkout's existing assets, settings and saves.
Ordinary executable launches use per-user data and do not silently copy files
from a checkout. Keeping this command is a trial for development convenience,
not a permanent product requirement: revisit or remove it if the workflow feels
wrong. The application must remain independent of the helper.

Existing documented launch-only display overrides remain specific to that launch;
pressing Play must not inadvertently make a command-line override persistent.

## First-run setup and recovery

When assets are missing, the launcher shows ADF setup and initially focuses
Choose ADF. Settings remain usable, and Play is unavailable until import
succeeds. Selecting an ADF immediately validates and imports it.

Unsupported disks, unreadable files and damaged imported assets produce an
explanation and retry action within the launcher. A failed replacement preserves
an existing valid asset pack. Play remains available whenever a valid pack is
available. The original ADF is always preserved.

## Launcher layout

Use one screen: game-data status near the top, the six grouped settings below,
and Play and Quit at the bottom. Once imported, show Game data ready, the source
filename and Change ADF. Keep technical diagnostics under Details.
Keyboard and controller focus highlights follow the active control; repaint
both focus gain and loss so no outline remains on a previously visited control.

The launcher is a contemporary native desktop surface with restrained Amiga-era
colour and typography. Its layout and status messages work before any source
disk is available. The original game's artwork and menus retain their existing
presentation after Play.

There is no Native port / Amiga original subtitle. The supplied original logo
forms a low-opacity background behind the title and fades into the dialog.
Scale the complete image to fit the padded header at the right, preserving its
aspect ratio without cropping at normal or compact window sizes.
It is a local build resource, kept out of source control with other original assets.

## Delivery and verification

Deliver a directly runnable built Windows application and updated documentation.
Installers and release publication are separate work. Build and verification
tooling remain development concerns, with the existing direct-race and replay
viewers retaining their distinct roles.

Implementation verification must cover fresh setup, supported and unsupported
disks, unreadable files, cancelled browsing, damaged packs, failed replacement
and retry. Verify that the ADF remains unchanged and a retained pack works after
the ADF is removed. Check shared settings, defaults, discarded edits, failed
preference writes and failed game initialization. Inspect mouse, keyboard and
controller navigation, focus transitions, DPI/layout and window/fullscreen
behaviour. Entering the game must release held launcher input and preserve the
existing gameplay, audio and presentation-clock contracts. Run the focused game,
startup, display and asset-policy checks appropriate to the implementation.

## Design tree

The product scope, application lifetime, player storage, preference ownership,
asset setup/recovery, layout and delivery branches are settled. The development
helper is explicitly provisional. The shared understanding and the logo treatment
were confirmed before implementation; future changes must respect these contracts
and the existing component guides.

## Existing implementation constraints

The executable currently requires an asset-pack argument and constructs its game
session before creating a window. ADF import exists in Python tooling; native
pack validation and the verified extraction recipe can be reused without
rediscovering disk contents. The launcher must exist before asset-dependent game
initialization so setup failures can be recovered within the UI.

Preferences and saves currently default to working-directory-relative paths.
The game also creates development driving recordings for every race. The agreed
design changes normal default paths and makes recordings explicit. Existing
settings have atomic persistence and Apply/Cancel behaviour; reuse these contracts
under the shared preference ownership defined above.

See [current settings](../playing.md), [assets](../components/native-startup.md) and
[architecture](../architecture.md) for the implemented contracts. The agreed runtime
boundary is recorded in [ADR 0001](../components/0001-native-application-owns-asset-import.md),
and player storage in [ADR 0002](../components/0002-per-user-player-data.md).

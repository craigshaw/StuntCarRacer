# Native original menus and local racing

Double-click `Drive Native.cmd`, or run:

```powershell
.\scripts\start-native-game.ps1
```

The native game opens the original game-mode chooser. It uses the imported ADF
pack and ported C++ routines throughout. No captured memory, Kickstart ROM,
WinUAE or CPU emulator is an input to this executable. The import/build/check
script remains development tooling; this is the game's menu, not an asset-path
launcher or settings application.

## Controls and original route

Select **Single Player League**, type a name and press Return or controller A.
The original name editor accepts up to twelve characters, keeps their case,
uses Shift for capitals and Backspace for deletion, and retries empty names.
Letters, digits, spaces and the original `.`, `/` and `:` punctuation are
accepted; leading spaces are ignored. Typing is independent of the racing
physics clock, and letters such as WASD, M and I reach the name editor.
Continue past the original driver portrait roster with a fresh fire press to
reach the main menu. Its original palette fades and release waits are retained.

**Multiplayer** retains the original name/add-player loop for two through eight
local drivers, then shows their startup standings. Continue requires at least
two names; the eighth name finishes entry automatically. This setup retains all
names and supports Practice and complete local championships. Each driver takes
a turn against the computer pace car on each of the division's two tracks.
The **Computer Link** serial transport remains unavailable.

Use Up/Down or the controller stick/D-pad to select, then Return/Space or the
controller A button to confirm. Number keys 1–4 select a row; confirmation is a
separate action. Navigation clamps at the first/last option. The original
release waits and approximately 326 ms repaint delay are retained, so held
input deliberately does not scroll continuously.

Choose **Hall of Fame** to view the lap and three-lap race records for all eight
tracks. It retains the original artwork, twelve-character driver names, time
formatting and palette fades. Release fire, then press Return/Space/controller A
again to return to the main menu. It displays the current car context's record
bank and retains competitive records throughout the session. Use a save name
beginning with `HALL` in the Load/Save/Replay route to retain them between launches.

**Load/Save/Replay** opens the original four-row submenu with Cancel selected.
Load and Save retain the original save-disc prompt; press any key to use the
local save file. The thirty slots occupy three columns. Up/Down moves through
them with wrapping; Left/Right moves between columns. Save names have eight
characters, use uppercase letters and support Backspace. Return or controller
fire accepts a name or loads a populated slot; Escape cancels slot selection.
The original unformatted-disc warning appears when first saving to a new file.

A normal save name stores the original career state: player name, driver order,
division, remaining damage allowance and league context. Names beginning with
`HALL` save both record banks; loading merges better records and keeps current
holders on ties. Names beginning with `MP` save the local multiplayer names,
scores and damage. The original compatibility warning enforces the player mode.
Career operations return through the original driver roster, multiplayer data
through standings, and `HALL` directly to the main menu.

**Replay** restores the retained season setup, resets the race tables and shows
the original roster. Holding F1 when confirming Replay returns to the original
mode/name startup. The development recording viewer remains available separately.

The default save file is `working/saves/stunt-car-racer.scr-save`. The development
script accepts `-SaveFile` for another `.scr-save` path. Saving replaces a complete,
flushed temporary file in one operation; failed writes retain the previous file.
The native loader rejects malformed states before applying them. Saves and
temporary save files are ignored by Git and rejected by the asset safeguard.

Choose **Practise**, then a division and one of its two tracks. The original
track overview appears. Left/Right rotates it by quarter turns; release before
another turn. Return/Space/A starts driving after release. Arrow keys or WASD
drive, Space boosts, and XInput controllers retain the existing stick/D-pad,
RT/LT and A mappings. Acceleration remains latched as in the original.

Escape during a race requests the original quit path; its original gameplay
gates still apply. Controller Back/View maps to that exit. At the end message,
release fire and press it again. The original exit cleanup saves RNG state and
restores the player's persistent data. Practice returns directly to the same
division's track selection. Select **Cancel** there to return to the main menu.
Choosing a track again creates a fresh race inside the same menu session.

**Start the Racing Season** enters the original local season against computer
opponents. Continue through the matchup and track overview to start a race.
The opponent follows the original speed, suspension and positioning routines;
its car is drawn in the original road painter order. Race completion or an
early quit proceeds through the original results, records and standings pages
before the next matchup. Single-player seasons retain six rounds, including the
original computer-only fixtures, followed by promotion/relegation and the roster.
Holding F1 when confirming a matchup forfeits the remaining fixtures and goes
directly to the season-end pages. It retains the original scoring and promotion
rules. Winning Division 1 in the normal league begins the Super League in
Division 4. A Super League championship uses its original victory message and
skips the separate division-winner award artwork.

In multiplayer, all drivers race the first track before moving to the second.
Each race has its original results, standings and time table. The track summary
awards the original best race and best lap bonuses. Damage persists separately
for each driver. Multiplayer has no promotion or relegation; load a single-player
career to access its division and car context while retaining the local names.
Scores accumulate across four championships before the original reset. `MP`
saves continue the names, scores and damage between launches; load a career again
to restore later-track access. Holding F1 when confirming a matchup forfeits the
remaining turns on that track and proceeds to its summary.

P/controller Start opens the native pause panel while driving. Choose Resume or
Display Settings with Up/Down and Return/A. P, Start, Escape or B resumes from
that panel. Focus loss and controller disconnection also pause driving; resume
explicitly. M mutes. I toggles and saves interpolation outside text entry.

F10 opens Display Settings from any screen. Controller Start opens it directly
from the original menus. Use arrows or the stick/D-pad to select rows; Left/Right
changes values, and Return/A changes a selected value or activates Apply and Back.
The options are Windowed/Borderless Fullscreen, Integer/Fit Sharp/Fit Smooth scaling,
and Original Updates/Smooth Interpolation. Changes preview immediately. Apply and
Back saves them; Cancel, Escape, B, F10 or Start restores the choices from before
entry. Settings entered from the pause panel return there. Focus loss while
adjusting settings during a race also requires an explicit resume.

F11 toggles borderless fullscreen on the window's monitor and restores the prior
window placement when leaving. Integer scaling uses uniform nearest-neighbour
enlargement; Fit Sharp uses nearest-neighbour at the largest fitting size;
Fit Smooth uses bilinear filtering. All retain the source aspect and black borders.
The original 320-by-200 game image and field of view are unchanged. Display UI is
composited after scaling, independently of the original game pixels.

Preferences are stored atomically in `working/settings/display.ini`. Invalid or
missing preferences use windowed, integer-scaled, original-update presentation.
Failed saves retain the last good file and show an error with an Apply retry.
`-SettingsFile` selects another `.ini` for development; `-Fullscreen` and
`-Interpolate` override the corresponding saved choices for that launch.
The standalone development replay viewer retains its existing window controls.
Both motion modes retain the verified 120 ms physics
updates and 50 Hz engine audio. The window presents at 60 Hz and reports FPS in
its title; host text and controls are kept outside the original game image.
No gameplay, fade, wheel or audio clocks advance while the settings overlay is
open, and held navigation/fire inputs must be released before gameplay accepts
them again. Closing settings retains the pre-pause fraction of a physics tick.

The cockpit uses the original font, speed bar, boost digits, lap counter,
running/completed time displays, distance and car icons. Their redraw cadence
and completed-lap hold follow the original game calls. Wheel treads animate at
50 Hz independently of camera interpolation, with the original forward/reverse
phases and suspension positions. Pausing retains the presented wheel phase.

## Implementation and fidelity

`startNativeGame` creates fresh asset/game data. `initializeNativeOpeningSession`
copies the original default names and record templates and prepares the opening
context. `NativeOpeningFlow` runs mode selection, the add-driver loop and roster
waits; `NativePlayerNameEntry` consumes original raw-key codes through the local
keyboard tables. `NativeOpeningNameScreen` applies accepted character and erase
events using original assets. Only after startup completes does `NativeMenuFlow`
open the main menu. The explicit `EstablishedMenu` session option retains the
earlier starting point for independent race regressions.

`NativeMenuFlow` ports menu selection, Practice and championship callers. The
[multiplayer state routines](native-multiplayer-state.md) retain the original
pace-car selection, scoring, sorting, damage and progression. The separate
[multiplayer renderer](native-multiplayer-render.md) draws its original portraits,
record pages and variable-height tables. `NativeMenuRender`
reads the original image, palette, strip graphics, strings, fonts and track-name
tables from the local pack. `NativeTrackPreview` owns separate rendering scratch.
`NativeGameSession` transfers selected track configuration into native race
initialization and transfers the recovered exit state back to the menus.
`native_game.exe` supplies the Windows event loop, input, audio device and image
presentation; it does not link the original-CPU verification library.

Presentation prepares the game image and letterboxing in an off-screen GDI
buffer, then copies the completed frame to the window in one operation. The
last frame remains visible during rendering; clearing the visible window before
drawing could otherwise expose intermittent black flashes to the compositor.

All eight tracks are available,
including the original [Draw Bridge animation](draw-bridge.md). Its preview
holds the original raised pose; movement starts during driving. The
original menu has no Escape-to-cancel shortcut on the overview.

This work does not establish pixel identity for the existing in-race 3D renderer.
The [presentation boundary](native-presentation.md) keeps simulation accuracy
independent of future camera and aspect-ratio changes. Practice and racing use
the same recovered road painter, and the horizon retains its original mode
offset. The [rendering audit](native-render-audit.md) checks scene layering,
camera-query isolation and representative frames across both leagues.
Its established geometry/projection checks and documented raster limits still
apply. Static menu/message pixels, preview pixels, navigation, and palette
transitions have independent original-code checks. Menu busy-loop timing uses
measured CPU clocks and PAL frequency; native rendering cost, Amiga bus
contention and free-running idle-poll RNG advances are not reproduced.

## Verification

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_game.py
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_game.py --verify-original
```

The ordinary check runs the complete route twice on all eight tracks,
checks overview rotation, fresh race counters, quit cleanup and return to the
unchanged main menu. Sixteen Hall of Fame visits cover entry, held fire,
debounce and return before and after racing; eight more visits follow the
original single-player and local multiplayer name-entry routes. It also runs the 4,096-update fresh-start and steering
regression. Screenshots, controls and source-identified results remain under
ignored `analysis/native-game/`.

It also runs the original opening through 36 entered names, every local player
count, single-player portraits and local standings, then drives three named
race routes. The optional check compares those race traces with the original.
The multiplayer check completes eleven championships and 88 races across all
seven player counts, including a pace-car finish, fourteen forfeited tracks,
career track access, fresh-launch `MP` continuation and the fourth-season reset.
Its 7,137 physics updates also run against the original full loop. Separate
original-code oracles check multiplayer state and 4,928 complete screen images.
The career check loads each division in both leagues, drives eight fresh races,
forfeits sixteen seasons and checks relegation, eight saves, sixteen loads and
eight Replay routes. Its 520 physics updates are compared with the original
full loop. Separate [career state](native-career-state.md) checks use controlled
race outcomes to exercise complete winning careers through both championships,
plus losing careers, damage and continuation between seasons. These are
state-transition tests rather than claims of human-driven championship wins.
The [career screen checks](native-career-render.md) compare 476 complete
competition screens, including every finishing place and championship branch.
The dedicated opening check additionally compares keyboard scanning, editing,
mode flow, menu/name/standings pixels and palette routines against original code:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_opening.py
```

Its source/asset/executable-identified receipt and screenshots remain under
`analysis/native-opening/`. Name and screen evidence:

- [Original opening flow and fade order](native-opening-flow.md).
- [Raw keyboard scanner and player-name rules](native-player-name.md).
- [Opening and name-entry pixels](native-opening-render.md).

The ordinary check additionally drives a synthetic approach to Draw Bridge,
checks occupancy/release and verifies stable interpolated and retained frames.
Display checks cover all twelve preference combinations, corrupt files, failed
atomic writes, keyboard/controller navigation, twelve scaling surfaces and four
fullscreen/window placement roundtrips. A paused race is presented 120 times,
then resumes on its original fractional tick boundary and exits with RAM identical
to a headless twin.
The optional check runs the menu, input, overview, palette, Hall of Fame and bridge oracles
against unmodified original MC68000 bytes. It also compares every recorded first
race entered from the menu, all fresh-start races and the bridge approach
against the original full game loop. The ordinary check also completes a local
season: one opponent finishes three laps, followed by three early quits and
the original results, promotion/relegation and roster flow. The optional check
compares all four menu-entered competitive races against the original loop.
Save regressions additionally create four saves, load five states across fresh
launches, drive three loaded careers, visit three Replay routes and exercise
cancel, damaged-data rejection, write retry and abort. All thirty file slots are
checked across repeated store instances, including interrupted and failed writes.
The optional check compares the three loaded-race traces, 800 save-codec RAM
cases and 1,364 save-menu screens against original instructions.
Test-only CPU and
hardware wait models are isolated from the native game. Component evidence:

- [Menu and message pixels](native-menu-rendering.md).
- [Input, selection and Practice callers](native-menu-flow.md).
- [Track preview](native-track-preview.md).
- [Palette transitions](native-palette-fade.md).
- [Hall of Fame display and transitions](native-hall-of-fame.md).
- [Hall of Fame record handling](native-hall-records.md).
- [Load/save screens and controls](native-save-menu.md).
- [Original save formats and Replay](native-save-state.md).
- [Native save-file backing](native-save-store.md).
- [Local seasons and original competition screens](native-opponent-races.md).
- [Cockpit instrument pixels](native-cockpit-instruments.md).
- [Instrument events and lap-time holds](cockpit-timing.md).
- [Wheel animation and hardware sprites](wheel-animation.md).

The cockpit check compares the original instrument buffers, ordered drawing
events and wheel interrupt states across fresh Practice and competitive starts
on all eight tracks, restarts, a complete three-lap replay, an opponent finish
and car contact. It also checks the corresponding gameplay traces:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_cockpit.py
```

The opponent's AI, collision, car model and far/near drawing queues have a
dedicated check, including all eight tracks, an opponent race finish and natural
car contact from digital driving inputs:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_opponents.py
```

The local asset pack, images, sounds, extracted strings and generated binary
outputs remain ignored. Only the importer, native algorithms, tests, provenance
and documentation belong in Git. `scripts/start-native-race.ps1` still opens the
direct-race development viewer, and `Watch Native Replay.cmd` retains the
independent captured replay regression.

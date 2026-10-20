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
names and supports Practice. Racing a multiplayer season and the **Computer
Link** serial transport produce explicit host notices at their unported boundaries.

Use Up/Down or the controller stick/D-pad to select, then Return/Space or the
controller A button to confirm. Number keys 1–4 select a row; confirmation is a
separate action. Navigation clamps at the first/last option. The original
release waits and approximately 326 ms repaint delay are retained, so held
input deliberately does not scroll continuously.

Choose **Hall of Fame** to view the lap and three-lap race records for all eight
tracks. It retains the original artwork, twelve-character driver names, time
formatting and palette fades. Release fire, then press Return/Space/controller A
again to return to the main menu. It displays the current car context's record
bank and retains competitive records throughout the session. Disk persistence
belongs to the remaining load/save work.

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
before the next matchup. The season retains six rounds, including the original
computer-only fixtures, followed by promotion/relegation and the driver roster.

P/controller Start pauses; focus loss and controller disconnection also pause
driving. Resume explicitly. M mutes. I toggles interpolation. The native game
defaults to original tick presentation; `-Interpolate` on the development script
starts with smooth presentation. Both modes retain the verified 120 ms physics
updates and 50 Hz engine audio. The window presents at 60 Hz and reports FPS in
its title; host text and controls are kept outside the original game image.

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

`NativeMenuFlow` ports menu selection, Practice and local season callers. `NativeMenuRender`
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

The load/save/replay route retains its original appearance and produces an
explicit native-host notice when activated. All eight tracks are available,
including the original [Draw Bridge animation](draw-bridge.md). Its preview
holds the original raised pose; movement starts during driving. The
original menu has no Escape-to-cancel shortcut on the overview.

This work does not establish pixel identity for the existing in-race 3D renderer.
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
The optional check runs the menu, input, overview, palette, Hall of Fame and bridge oracles
against unmodified original MC68000 bytes. It also compares every recorded first
race entered from the menu, all fresh-start races and the bridge approach
against the original full game loop. The ordinary check also completes a local
season: one opponent finishes three laps, followed by three early quits and
the original results, promotion/relegation and roster flow. The optional check
compares all four menu-entered competitive races against the original loop.
Test-only CPU and
hardware wait models are isolated from the native game. Component evidence:

- [Menu and message pixels](native-menu-rendering.md).
- [Input, selection and Practice callers](native-menu-flow.md).
- [Track preview](native-track-preview.md).
- [Palette transitions](native-palette-fade.md).
- [Hall of Fame display and transitions](native-hall-of-fame.md).
- [Hall of Fame record handling](native-hall-records.md).
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

# Native original menus and local racing

Double-click `Drive Native.cmd`, or run:

```powershell
.\scripts\start-native-game.ps1
```

The native game opens the original local main menu. It uses the imported ADF
pack and ported C++ routines throughout. No captured memory, Kickstart ROM,
WinUAE or CPU emulator is an input to this executable. The import/build/check
script remains development tooling; this is the game's menu, not an asset-path
launcher or settings application.

## Controls and original route

Use Up/Down or the controller stick/D-pad to select, then Return/Space or the
controller A button to confirm. Number keys 1–4 select a row; confirmation is a
separate action. Navigation clamps at the first/last option. The original
release waits and approximately 326 ms repaint delay are retained, so held
input deliberately does not scroll continuously.

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

## Implementation and fidelity

`startNativeGame` creates fresh asset/game data. `initializeNativeMenuSession`
copies the original default names and record templates, then enters an
established local-player main-menu context. The original local-player slot and
its empty default name are retained. Initial mode selection and driver-name
entry are not part of this milestone.

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

The first main menu's other routes retain their original appearance but produce
an explicit native-host notice when activated: Hall of Fame and load/save/replay
are not implemented here. All eight tracks are available,
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
unchanged main menu. It also runs the 4,096-update fresh-start and steering
regression. Screenshots, controls and source-identified results remain under
ignored `analysis/native-game/`.

The ordinary check additionally drives a synthetic approach to Draw Bridge,
checks occupancy/release and verifies stable interpolated and retained frames.
The optional check runs the menu, input, overview, palette and bridge oracles
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
- [Local seasons and original competition screens](native-opponent-races.md).

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

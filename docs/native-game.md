# Playing the native game

Double-click `Drive Native.cmd`, or run:

```powershell
.\scripts\start-native-game.ps1
```

The native game opens the original game-mode chooser. It uses the imported ADF
pack and ported C++ routines throughout. No captured memory, Kickstart ROM,
WinUAE or CPU emulator is an input to this executable. The import/build/check
script remains development tooling; asset-path selection is not part of the
game UI. Display and control preferences have a separate native overlay.

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
drive and Space boosts in either control preset. XInput steering uses the left
stick or D-pad; A or RB boosts. Choose a preset in Settings:

| Preset | Accelerate | Brake / reverse |
| --- | --- | --- |
| Original (default) | Stick or D-pad up | Stick or D-pad down |
| Modern | Right trigger | Left trigger |

Original ignores triggers while driving. Modern ignores stick/D-pad up/down
for throttle, so diagonal steering cannot accelerate or brake. Triggers are
digital: values above 30 count as pressed, with no pressure-dependent strength.
Stick driving axes use a 7,849-unit deadzone. Opposing steering cancels and
brake takes priority over gas, including combined keyboard/controller input.
Original retains latched acceleration: releasing gas keeps accelerating until
braking. Modern accelerates only while RT (or keyboard gas) is held and coasts
on release, without applying the brake. Releasing before the next physics tick
cancels Modern's pending gas press. Boost alone does not restart acceleration
while coasting. Menus and text entry retain their controls.

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
Settings with Up/Down and Return/A. P, Start, Escape or B resumes from
that panel. Focus loss and controller disconnection also pause driving; resume
explicitly. M mutes. I toggles and saves interpolation outside text entry.

F10 opens Settings from any screen. Controller Start opens it directly
from the original menus. Use arrows or the stick/D-pad to select rows; Left/Right
changes values, and Return/A changes a selected value or activates Apply and Back.
The options are Windowed/Borderless Fullscreen, Integer/Fit Sharp/Fit Smooth scaling,
Original Updates/Smooth Interpolation, and Original/Modern controls. Changes
preview immediately. Apply and Back saves them; Cancel, Escape, B, F10 or Start restores the choices from before
entry. Settings entered from the pause panel return there. Focus loss while
adjusting settings during a race also requires an explicit resume.

F11 toggles borderless fullscreen on the window's monitor and restores the prior
window placement when leaving. Integer scaling uses uniform nearest-neighbour
enlargement; Fit Sharp uses nearest-neighbour at the largest fitting size;
Fit Smooth uses bilinear filtering. All retain the source aspect and black borders.
The original 320-by-200 game image and field of view are unchanged. Display UI is
composited after scaling, independently of the original game pixels.

Preferences are stored atomically in `working/settings/display.ini`. Invalid or
missing preferences use windowed, integer-scaled, original-update presentation
and Original controls. Older display-only preferences retain their display
choices and use Original controls; saving upgrades them to include the preset.
Failed saves retain the last good file and show an error with an Apply retry.
`-SettingsFile` selects another `.ini` for development; `-Fullscreen` and
`-Interpolate` override the corresponding saved choices for that launch.
The standalone development replay viewer retains its existing window controls.
Both motion modes retain the verified 120 ms physics
updates and 50 Hz engine audio. The window presents at 60 Hz and reports FPS in
its title; host text and controls are kept outside the original game image.
No gameplay, fade, wheel or audio clocks advance while the settings overlay is
open, and held navigation, fire and trigger inputs must be released before
gameplay accepts them again. Closing settings retains the pre-pause fraction of a physics tick.

The cockpit uses the original font, speed bar, boost digits, lap counter,
running/completed time displays, distance and car icons. Their redraw cadence
and completed-lap hold follow the original game calls. Wheel treads animate at
50 Hz independently of camera interpolation, with the original forward/reverse
phases and suspension positions. Pausing retains the presented wheel phase.
The hanging start and crash-recovery frames show the original blinking
DROP START and PRESS FIRE prompts. Off-track ground contact draws the original
animated dust bitmaps; edge impacts draw the recovered sparks. Their visibility,
animation and ordered draw positions belong to completed gameplay snapshots,
so interpolation and paused or repeated views never advance them or the RNG.

## Architecture and verification

[Architecture](architecture.md) describes session ownership, the native game loop,
independent clocks and fidelity boundaries. The [component index](README.md)
links original-code evidence for menus, career, multiplayer, saves, cockpit and
rendering. The game has no runtime verification CPU dependency.

From the project root:

```powershell
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_game.py
& C:/Tools/amiga-re/venv/Scripts/python.exe scripts/check_native_game.py --verify-original
```

The ordinary suite checks first and repeated races on every track, fresh startup,
opening/name routes, Draw Bridge, local seasons, career continuation, multiplayer,
saves, digital control presets, preference migration and settings/pause isolation.
The optional suite adds original-code state,
pixel and independently evolved driving comparisons. The separate cockpit suite
checks both instrument buffers, their original events and wheel timelines,
including first/repeated menu-entered races and the complete three-lap fixture.

See [development](development.md) for prerequisites, focused checks and receipt
interpretation. Reports and generated images remain under ignored `analysis/`.
`scripts/start-native-race.ps1` opens the direct-race development viewer;
`Watch Native Replay.cmd` opens the captured authored replay. Their controls are
listed in their own guides, rather than the game controls above.

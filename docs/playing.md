# Playing the native game

Open `dist/Stunt Car Racer.exe` directly after [building and staging](development.md#build-test-and-stage). For the
prepared development workflow, `Drive Native.cmd` builds/checks the game and
opens the same launcher with checkout-local data, or run:

```powershell
.\scripts\start-native-game.ps1
```

The executable opens a native launcher before constructing the game session.
Choose ADF imports the supported original disk, preserving it and retaining an
asset pack for later launches. Missing or damaged data keeps Play unavailable;
failed replacements retain existing valid data. Details shows the supported
source hash and local pack path. The original logo is a subdued header background,
scaled proportionally to fit within the header.
Mouse, Tab, arrows and controller navigation operate the launcher; Windows' file
picker uses its normal keyboard/mouse controls.

The launcher offers display mode, scaling, 60 FPS smoothing (Off/On), driving
controls and menu navigation. Smoothing On interpolates animation at 60 FPS;
Off retains the original stepped animation. Both
keep the same game speed and 60 Hz host presentation. Changes
are saved when Play is pressed; closing discards pending setting edits while
retaining completed asset imports. Restore original defaults resets only those
choices, including Integer Auto and Original menu navigation. Failed saves and game initialization remain in the launcher with
an explanation and retry. The launcher stays windowed; Play applies the selected
display mode and enters the original mode chooser in the same window. In-game
Settings remains available, and closing the game exits the application.

Normal executable launches use `%LOCALAPPDATA%/Stunt Car Racer/`: `assets/` for
the pack and provenance, `settings/display.ini` for preferences and
`careers/`, `last.scr-index` and `hall.scr-hall` for automatic persistence.
`saves/stunt-car-racer.scr-save` retains manual multiplayer saves. Drive Native.cmd
isolates careers/Hall under `working/player-data/`; `-DataDirectory` or the native
`--data-dir` argument selects another location. A second launch using the same
data directory brings the existing player forward. No captured memory,
Kickstart ROM, WinUAE, Python or CPU emulator is needed by the executable.
Automatic driving recordings are off; `--recordings-dir` explicitly enables them
for development. The direct-race and captured replay viewers retain their controls.

## Controls and original route

Select **Single Player League** to open career selection every time.
**New Career** always appears; **Continue** appears when the last-used career is
available, and **Browse Careers** appears when any saved careers exist. With no
careers, New Career is the only row. Choose it to enter a name and press Return
or controller A.
The original name editor accepts up to twelve characters, keeps their case,
uses Shift for capitals and Backspace for deletion, and retries empty names.
Letters, digits, spaces and the original `.`, `/` and `:` punctuation are
accepted; leading spaces are ignored. Typing is independent of the racing
physics clock, and letters such as WASD, M and I reach the name editor.
Continue past the original driver portrait roster with a fresh fire press to
create the first autosave and reach the single-player main menu. A failed first save offers
Retry/Cancel before racing. Responsive navigation removes deliberate waits and blocking
palette fades; Original navigation retains them.

**Multiplayer** retains the original name/add-player loop for two through eight
local drivers, then shows their startup standings. Continue requires at least
two names; the eighth name finishes entry automatically. This setup retains all
names and supports Practice and complete local championships. Each driver takes
a turn against the computer pace car on each of the division's two tracks.
The **Computer Link** serial transport remains unavailable.

Use Up/Down or the controller stick/D-pad to select, then Return/Space or the
controller A button to confirm. Number keys 1–4 select a row; confirmation is a
separate action. Navigation clamps at the first/last option. The original
release waits and approximately 326 ms repaint delay remain available under
Original menu navigation. Responsive is the default: presses act immediately,
and held directions repeat after 300 ms, then every 100 ms. Confirm and Back
require fresh presses. Controls held across a screen change require release;
other fresh presses still work immediately.

In Responsive menus, **Escape / controller B** goes back one logical level and
restores the previous selection. Existing Cancel items follow the same route.
Player setup lets you revisit names; Backspace still deletes text. Back does
has no parent at the mode chooser; Escape there opens the native Game menu,
while controller B does nothing. In single-player careers, Back from a championship
matchup, finish message or report returns to the profile's main menu, preserving
the exact pending stage. **Resume Season** restores it; A/Enter advances reports.
Back from the main menu returns to career selection, then to the mode chooser. These suspension
routes work with either navigation policy. Back on a championship preview returns
to its prepared matchup without preparing it again. Multiplayer retains its
existing matchup/report acknowledgement flow.

Choose **Hall of Fame** to view the lap and three-lap race records for all eight
tracks. It retains the original artwork, twelve-character driver names, time
formatting and palette fades. Release fire, then press Return/Space/controller A
again to return to the main menu, or use Escape/B in Responsive mode. It displays the current car context's record
bank. Eligible human lap and three-lap competitive records save automatically in
both leagues and both modes. An earned lap survives closing during the remaining
race. Practice, partial laps and opponents cannot add records; ties retain the
existing holder. Deleting or restoring a career cannot roll the shared Hall back.

## Careers and autosave

**Continue** opens the last-used career's main menu. It does not jump straight
into a race or report. **New Career** creates an independent driver identity and
progress across seasons; two careers may have the same name. **Browse Careers** is sorted by last played,
with the selected driver's division, league and date beneath the list. Up/Down
scrolls and Left/Right moves three careers at a time. Select a career to continue
or delete it. Delete requires confirmation, removes its recovery copies and keeps
the shared Hall. Rename is not offered. Deleting the last-used career requires
explicitly choosing another before Continue appears again.

An open career's main menu has three rows: **Hall of Fame**, **Practise** and
**Start Season**. When a season is suspended, the third row becomes **Resume
Season** and reopens its exact matchup, finish message, results, standings, award
or roster. There is no fourth Career row; Escape/B returns to career selection.
Autosave is always enabled and does
not depend on the navigation or driving preset. There is no single-player manual
save/load/Replay route or import of old saves/HALL records. Existing legacy files
remain untouched. To switch careers during a season, back out through the main
menu to career selection and browse or create another career.

Closing or crashing during an unfinished competitive race restarts the same
fixture at its matchup through Resume Season, with the pre-race state and damage.
Continue always opens the main menu first. A completed race is saved before its finish message
is acknowledged, and its result is scored once. Practice exits normally to track
selection and retains its resulting RNG in the profile's main-menu state. A
suspended season keeps its own exact state and RNG throughout Practice, including
closing during Practice. Resume Season restores that saved season; Practice
cannot overwrite it or score points. The existing Pause and Leave Race controls
retain their racing behaviour.

Saving/Saved appears in career panels and the window title in menus or pause.
**Not saved** stays visible after failure. At a safe menu choose Retry or Continue
without saving; the latest progress remains in RAM for Continue. Future checkpoint
boundaries retry. Switching careers or closing flushes writes; a failed flush
offers Retry, Leave unsaved or Cancel. Leaving unsaved can lose progress since the
last successful write.

Each store retains two prior valid generations. A damaged/missing current file
recovers the newest valid copy with a notice. A career with no usable copy stays
unavailable; choose, create or delete explicitly. A damaged Hall offers an explicit
fresh start while preserving the damaged files. Unsupported formats are not
overwritten. See [storage and verification](components/native-career-persistence.md).

## Manual multiplayer saves

**Load/Save/Replay** opens the original four-row submenu with Cancel selected.
In multiplayer, Load and Save retain the original save-disc prompt; press any key to use the
local save file. The thirty slots occupy three columns. Up/Down moves through
them with wrapping; Left/Right moves between columns. Save names have eight
characters, use uppercase letters and support Backspace. Return or controller
fire accepts a name or loads a populated slot. In Responsive mode, confirm a
save slot before editing its name; Escape/B returns from the editor to the slot
list, then to Load/Save, then main. Unconfirmed edits are discarded; completed
saves/loads remain committed. Original navigation retains its existing editor
and Escape cancellation.
The original unformatted-disc warning appears when first saving to a new file.

A normal save name retains the manual access/career data: player name, driver
order, division, remaining damage allowance and league context. Names beginning
with `HALL` are retired: records save automatically and legacy record files are
not imported. Names beginning with `MP` save the local multiplayer names,
scores and damage. The original compatibility warning enforces the player mode.
Career operations return through the original driver roster, multiplayer data
through standings.

**Replay** restores the retained season setup, resets the race tables and shows
the original roster. Holding F1 when confirming Replay returns to the original
mode/name startup. The development recording viewer remains available separately.

The development helper's save file is `working/saves/stunt-car-racer.scr-save`. Its
script accepts `-SaveFile` for another `.scr-save` path. Saving replaces a complete,
flushed temporary file in one operation; failed writes retain the previous file.
The native loader rejects malformed states before applying them. Saves and
temporary save files are ignored by Git and rejected by the asset safeguard.

Choose **Practise**, then a division and one of its two tracks. The original
track overview appears. Left/Right rotates it by quarter turns. In Responsive
mode, Escape/B returns to track selection; track Cancel returns to division
selection. Original navigation retains its release waits and Cancel-to-main
route. Return/Space/A starts driving after release. Arrow keys or WASD
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

During an unfinished race, **Game menu → Leave Race** requests the original
race-exit path; its gameplay gates and scoring rules still apply. Controller
Back/View remains a direct shortcut to that exit. Escape opens the Game menu.
At the end message,
release fire and press it again. The original exit cleanup saves RNG state and
restores the player's persistent data. Practice returns directly to the same
division's track selection. In Responsive mode, Cancel/Back goes to divisions,
then Back returns to main. Original track Cancel returns directly to main.
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

**Escape** opens the native **Game menu** while racing or at the top-level mode
chooser. In nested original menus it keeps its existing Back/Cancel route.
**P** opens the Game menu elsewhere, and **controller Start** opens it from
both racing and menus. P types normally in player/save-name editors.

The Game menu offers **Resume**, **Settings**, **Leave Race** during an
unfinished race, and **Quit to Desktop**. Use arrows/D-pad and Enter/A.
Escape/B, P or Start closes it to resume. From Settings those keys go back one
level instead. Focus loss and controller disconnection also pause driving;
resume explicitly. M mutes. I toggles and saves interpolation outside text entry.

Quit to Desktop opens a confirmation with **Cancel** selected. Escape/B, P or
Start cancels back to the Game menu. The confirmation explains that an unfinished
career race restarts at its saved matchup, or that multiplayer progress since
the last manual save will be lost. Confirmed quitting uses the same career/Hall
flush as closing the window or Alt+F4. Failed saves retain the existing
Retry/Leave Without Saving/Cancel choice; cancelling a failed close returns to
the Game menu without resuming a race. Leave Race retains its existing scoring
consequences and does not close the application.

F toggles a small FPS counter at the top left of the game window, including
fullscreen. It starts hidden on each launch and shows host presentation frames
per second, averaged over roughly one second. It remains available in menus,
Pause and Settings. During name entry, F types normally instead of toggling it.

F10 opens Settings directly, except while confirming Quit. Settings is also
available from the Game menu. Use arrows or the stick/D-pad to select rows; Left/Right
changes values, and Return/A changes a selected value or activates Apply and Back.
The options are Windowed/Borderless Fullscreen, Integer/Fit/Stretch image sizing,
Integer scale or Filtering, 60 FPS smoothing Off/On, Original/Modern driving controls, and
Responsive/Original menu navigation, and 4:3 Original, 4:3 Extended or 16:9 views. Menu navigation is independent of the
driving preset; applying it keeps the current session and screen. Changes
preview immediately. Apply and Back saves them; Cancel, Escape, B, F10 or Start restores the choices from before
entry. Settings entered from the pause panel return there. Focus loss while
adjusting settings during a race also requires an explicit resume.

F11 toggles borderless fullscreen on the window's monitor and restores the prior
window placement when leaving. Image sizing and filtering are separate:

- **Integer** uses crisp nearest-neighbour enlargement at the selected aspect ratio. **Auto**, the default, selects
  the largest whole-number display scale that fits. Fixed **1×–32×** scales centre the
  image in fullscreen; selecting one in windowed mode resizes the game area
  (3× gives 960×720 for 4:3, 1280×720 for 16:9, or 2560×720 for 32:9). Oversized choices use the largest fitting
  whole scale. Manually shrinking a window can also limit the selected scale.
- **Fit** preserves the selected aspect ratio and fills whichever screen
  dimension limits it. At 1920×1080, 4:3 shows 1440×1080 with side borders;
  a 16:9 race fills the display.
  At 5120×1440, a 32:9 race fills the display.
- **Stretch** fills the entire window or monitor, changing image proportions.
  It shows the same game view and retains all HUD and menu content.
- **Filtering**, shown for Fit and Stretch, offers **Nearest neighbour** for
  unfiltered pixel edges or **Bilinear** for softer, blended edges. Integer
  always uses nearest-neighbour pixels. The last filter and integer scale are
  remembered separately when switching image sizing.

Settings preview immediately; Cancel restores the previous choices and window
placement, including after changing a fixed scale or fullscreen. Apply retains
the preview and saves it. Sizing and filtering do not change the field of view. Display UI is
composited after scaling, independently of the original game pixels.

**Aspect ratio** is a separate choice in the launcher and Settings:

- **4:3 (Original)** is the default and keeps the original viewport.
- **4:3 (Extended)** reveals scenery in the unused upper corners while
  retaining the original cockpit, car and instrument positions.
- **16:9** renders a wider world around the same-size original car and main
  arch. Its straight side braces and blue interior extend into the added space.
  The engine, tyres, exhausts and instruments keep their original artwork.
- **32:9** extends the same world and procedural frame farther sideways,
  retaining the centred car and main arch with no additional artwork edits.

All four retain the original car's pixel proportions. At a fixed display
height, the wide modes add space to either side while retaining the car's scale. Integer
sizing uses 240-pixel height steps: 4:3 widths are 320, 640, 960; 16:9 widths
are 427, 853, 1280; 32:9 widths are 853, 1707, 2560. Auto chooses the largest scale that fits the current window.
Menus, track previews and saved career finish screens stay 4:3 in all modes.
Changing aspect previews immediately; Cancel restores the previous choice.

Preferences are stored atomically; the development helper uses
`working/settings/display.ini`. Invalid or
missing preferences use windowed, integer-scaled, original-update presentation
and Original controls. Older display-only preferences retain their display
choices and use Original controls; saving upgrades them to include the preset.
Version 8 saves all four aspect modes. Versions 6 and 7 retain their existing choices;
versions 1–4 retain Original; the old
version-5 widescreen prototype selection migrates to Extended.
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
running/completed time displays, distance and car icons. The upper-right running
lap timer shows minutes, seconds and hundredths (`m:ss.hh`), refreshed at each
game update. It retains completed-lap times during the original hold and freezes
at the finish; records use the same game clock. Other instrument redraws follow
the original game calls. Wheel treads animate at
50 Hz independently of camera interpolation, with the original forward/reverse
phases. With 60 FPS smoothing On, hardware wheel detail follows the interpolated
suspension, dust and sparks move smoothly between game updates, and lifting
chains scroll out of view. Flame and dust bitmap shapes retain their original
animation phases. Pausing retains the presented wheel phase.
The hanging start and crash-recovery frames show the original blinking
DROP START and PRESS FIRE prompts. Off-track ground contact draws the original
animated dust bitmaps; edge impacts draw the recovered sparks. Their visibility,
animation and ordered draw positions belong to completed gameplay snapshots.
Smoothing uses those snapshots without advancing effects or RNG; particle
births, replacements and crash/restart transitions retain their game timing.

## Architecture and verification

[Architecture](architecture.md) describes session ownership, the native game loop,
independent clocks and fidelity boundaries. The [component index](README.md)
links original-code evidence for menus, career, multiplayer, saves, cockpit and
rendering. The game has no runtime verification CPU dependency.

From the project root:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_game.py
& $AmigaPython scripts/check_native_game.py --verify-original
```

The ordinary suite checks first and repeated races on every track, fresh startup,
native ADF import/recovery, launcher navigation, staged/shared preferences,
transient launch overrides, optional-recording state isolation and launcher rendering,
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

# Playing Stunt Car Racer

## Start

Open `dist/Stunt Car Racer.exe` after [building and staging](development.md#build-test-and-stage).
Choose your supported original ADF in the launcher, select settings and press
Play. The application validates the disk and retains its assets; the ADF stays
unchanged and is not needed on subsequent launches. Details shows the accepted
disk hash and imported pack path. No Kickstart ROM, emulator or Python is needed.

Missing/damaged assets disable Play until repaired. Failed import preserves an
existing valid pack. The launcher stays windowed; Play applies the chosen game
display mode in the same window. Mouse, Tab, arrows and controller operate it;
the Windows file picker uses its normal keyboard/mouse controls.

Launcher edits save on Play; closing discards edits while keeping successful
imports. Restore original defaults resets settings, including Original menu
navigation, without changing assets or saves. Missing/new preferences use
Responsive navigation. Failed settings writes or startup remain in the launcher
with an error and retry route.

## Controls

| Action | Keyboard | Xbox / XInput |
| --- | --- | --- |
| Navigate / confirm | Arrows, Enter or Space | Stick/D-pad, A |
| Back in Responsive menus | Escape | B |
| Accelerate / brake | Up/Down or W/S | Original: stick/D-pad up/down; Modern: RT/LT |
| Steer / boost | Left/Right or A/D, Space | Stick/D-pad, A or RB |
| Game menu | Esc in races/mode chooser; P outside text entry | Start |
| Settings | F10 or Game menu → Settings | Game menu → Settings |
| Fullscreen / smoothing / mute | F11 / I / M | Settings for fullscreen/smoothing |
| FPS counter | F | — |
| Leave race / quit | Game menu | Game menu; Back/View also has the original race-quit shortcut |

Text entry owns letters, including WASD, P, M, I and F. Names accept twelve
characters, retain case, use Shift for capitals and Backspace for deletion, and
retry empty input. Digits, spaces, `.`, `/` and `:` are accepted; leading spaces
are ignored. Menu numbers 1–4 select a row, then require confirmation.

Responsive navigation acts immediately, repeats a held direction after 300 ms
and every 100 ms thereafter, and adds Back with remembered selections. Confirm
and Back need fresh presses; held navigation must be released after a transition.
Original navigation retains recovered release waits, fades and cancellation.
This choice is independent of the driving preset.

## Careers and records

Single Player League opens career selection:

- **New Career** creates an independent driver, even when another has the same
  name. Confirm the name and portrait roster to create the first autosave.
  A failed first save offers Retry/Cancel before racing.
- **Continue** opens the last-used career's main menu, rather than jumping
  directly into a race or report.
- **Browse Careers** appears when careers exist, ordered by last played with
  division, league and date. Up/Down scrolls; Left/Right moves three entries.
  Delete requires confirmation and removes that career's recovery copies.
  Deleting the last-used career requires explicitly choosing another; Rename
  and import of original manual-slot careers are unavailable.

The career main menu has **Hall of Fame**, **Practise** and **Start Season**.
The third row becomes **Resume Season** while a season is pending. Back from
a matchup, finish or report returns to the main menu with that exact stage
suspended. Back from the main menu returns to career selection, then the mode
chooser. These career routes work with either navigation policy. Preview Back
returns to its prepared matchup; A/Enter advances reports.

Autosave is always active, independently of driving/navigation presets. Closing
or crashing during unfinished racing resumes the same fixture at its pre-race
matchup, with pre-race damage and state. Completion captures the finish and
result before acknowledgement, so restoring it cannot score twice. Practice
cannot replace or advance a suspended season, including when interrupted.
There is no single-player manual save/load/Replay route or legacy HALL import.

Hall of Fame shows the current league's lap and three-lap records for all eight
tracks. In Original navigation, release fire then press Enter/Space/A again to
return; Responsive also accepts Escape/B. Eligible competitive human records
save automatically across careers and multiplayer. An earned lap can survive
closing during the rest of its race. Practice, incomplete laps and opponents
cannot add records; ties keep the holder. Deleting/restoring a career cannot
remove or roll back shared records.

## Practice and seasons

Practise selects a division and one of its two tracks. Left/Right rotates the
overview; release then Enter/Space/A starts driving. Responsive Back returns from
preview to tracks, then divisions, then main. Original track Cancel returns
directly to main. Normal Practice exit returns to the same division's tracks;
choosing again creates a fresh race without discarding the career or season.

Original driving controls latch acceleration until braking. Modern accelerates
only while gas is held and coasts on release; boost alone does not restart gas.
Modern separates RT/LT throttle from stick/D-pad steering; Original ignores
triggers. Both presets also accept keyboard driving. Triggers are digital above
30, not proportional; the stick has a 7,849-unit deadzone. Opposing steering
cancels and brake takes priority over gas across keyboard/controller input.

Start Season proceeds through matchup, overview, racing, results, records and
standings. Single-player seasons have six fixtures, including computer-only
races, then promotion/relegation and the roster. Holding F1 while confirming a
matchup forfeits the remaining fixtures using the original scoring rules.
Winning normal Division 1 begins Super League Division 4. Super League victory
uses its original message without the separate division-winner award image.

Multiplayer supports two to eight local drivers taking turns against a computer
pace car. Add Player retains names; Continue requires two, and the eighth name
automatically completes setup. Every driver races the first track, then the
second. Race/track summaries retain wins, lap/race bonuses and individual damage.
Scores accumulate across four championships before reset; there is no promotion
or relegation. F1 at a matchup forfeits the remaining turns on that track.
Computer Link remains greyed out and unavailable.

## Game menu and leaving a race

Escape opens the Game menu in a race or mode chooser. In nested original menus
it retains Back/Cancel. P and controller Start open the Game menu from menus or
racing; P still types normally in name editors. Controller B has no parent at
the mode chooser. Focus loss or controller disconnection pauses driving and
requires explicit resume.

Resume returns to the underlying screen. Settings opens the shared display and
control options. Leave Race appears only while unfinished and exits immediately,
including during crash/lift/recovery; original damage and scoring still apply.
Back/View retains a separate original quit shortcut that can wait for recovery.
At a finish message, release fire and press again to continue.

Quit to Desktop starts with Cancel selected. Escape/B, P or Start cancels back
to the Game menu. Quitting an unfinished career race resumes later at its saved
matchup; multiplayer loses progress since its last manual save. Quitting, closing
the window and Alt+F4 all flush career/Hall writes. If saving fails, Retry/Leave
Without Saving/Cancel remains available; cancelling returns to the Game menu
without resuming racing. There is no return-to-launcher action.

## Manual multiplayer saves

Load/Save/Replay retains the original save-disc prompt; press a key to use the
local save file. Thirty slots occupy three columns: Up/Down wraps through rows,
Left/Right changes column. Names have eight uppercase characters and Backspace;
Enter/A confirms a name or loads a populated slot. Responsive confirms the slot
before editing and backs out editor → slots → Load/Save → main, discarding only
unconfirmed edits. Completed operations stay committed. Original retains its
editor and Escape handling. First save to a new file shows the format warning.

`MP` names save local names, scores and damage. Other allowed names retain the
original driver/access data, including division and league context; loading that
data in multiplayer preserves the local roster. MP files do not retain track
access or the four-championship counter, so restore access separately when needed.
`HALL` operations are retired in favour of automatic shared records. The original
mode-compatibility warning remains.

Replay restores the retained season setup and resets its race tables before the
roster/standings route. Holding F1 while confirming Replay returns to mode/name
startup. This is separate from the development recording viewer. Manual saves
replace a flushed temporary file atomically; failed writes retain the prior file,
and malformed states are rejected before application.

## Settings

F10 opens Settings except during Quit confirmation. Arrows/D-pad select rows;
Left/Right changes values and Enter/A changes a value or activates Apply and Back.
Changes preview immediately. Apply saves; Cancel/Escape/B/F10/Start restores
the previous choices and window placement. Settings entered from the Game menu
return there. Game, audio, fade and animation clocks stop while overlays are open;
held controls must be released before resuming, and the fractional tick survives.

| Setting | Choices and effect |
| --- | --- |
| Display mode | Windowed or borderless fullscreen on the window's monitor; F11 toggles and restores the prior placement |
| Image sizing | Integer, Fit or Stretch |
| Integer scale | Auto (default) or 1×–32×; largest fitting whole scale caps oversized choices |
| Filtering | Nearest neighbour or Bilinear for Fit/Stretch; Integer always uses nearest neighbour |
| 60 FPS smoothing | Off holds original update poses; On smooths completed snapshots without changing game speed |
| Driving controls | Original or Modern |
| Menu navigation | Responsive (default) or Original |
| Aspect ratio | 4:3 Original (default), 4:3 Extended, 16:9 or 32:9 |

Integer uses 240-pixel display-height steps. Fixed scales resize the windowed
game area and centre fullscreen output; 3× gives 960×720 in 4:3, 1280×720 in
16:9 or 2560×720 in 32:9. Auto selects the largest fitting scale. Fit preserves
aspect ratio, so 16:9 fills 1920×1080 and 32:9 fills 5120×1440. Stretch fills the
surface by changing proportions. Filter and integer-scale choices are remembered
independently. Sizing/filtering do not change the field of view.

Extended reveals scenery in the black upper corners. Wide modes add world and
extended side braces/blue interior around the same-size original car and arch.
At equal display height the car retains its scale. Menus, previews and saved
finish screens remain 4:3. Aspect changes preview immediately and Cancel restores
the old choice. See [view details](components/presentation.md#views).

Gameplay stays at 120 ms, wheel/engine state at 50 Hz, and host presentation
targets 60 FPS. Smoothing blends camera/suspension, wheel placement, particles
and lifting chains; rotation, image phases and gameplay events keep their own
timing. The running lap timer shows `m:ss.hh` at game-update cadence and retains
completed-lap holds. DROP START/PRESS FIRE, dust and sparks retain their original
conditions. F toggles the initially hidden host FPS counter, averaged over one
second; it is independent of simulation and types normally during name entry.

## Data and recovery

Normal launches use `%LOCALAPPDATA%/Stunt Car Racer/`:

| Location | Content |
| --- | --- |
| `assets/` | Imported pack and provenance |
| `settings/display.ini` | Shared preferences |
| `careers/`, `last.scr-index`, `hall.scr-hall` | Native careers, last-used identity and shared records |
| `saves/stunt-car-racer.scr-save` | Manual multiplayer saves |

A second launch using the same data directory brings the existing window forward.
Saving/Saved appears in menus/pause; Not saved persists after failure. Retry or
Continue without saving retains current RAM progress, and later checkpoints retry.
Switching careers or closing waits for writes and offers Retry/Leave unsaved/Cancel
if needed. Leaving unsaved can lose progress since the last successful write.

Careers, the last-used index and shared records keep two previous valid generations.
A damaged/missing current file recovers the newest valid copy with a notice.
With no usable copy a career stays unavailable until explicit selection/creation/deletion;
an unrecoverable Hall offers a confirmed fresh start while preserving damaged files.
Unsupported formats are left untouched.
Preferences also replace files atomically; invalid/missing preferences use defaults,
while supported older versions migrate. Failed Apply offers retry.

See [development](development.md) for checkout-local paths, command-line overrides,
recording and checks. The direct-race and captured replay viewers have different
controls, documented in [development tools](../tools/README.md#viewers).

# Menu navigation design

Status: implemented after confirmation of shared understanding.

This design records navigation for the faithful baseline flow. The player's
current single-player hierarchy is defined by the
[career persistence design](career-persistence-design.md): career selection,
the three-row main menu and Resume Season. Its Back/suspension routes apply in
both navigation timing policies; the baseline route table below remains useful
for multiplayer and original-code comparisons.

The next quality-of-life improvement targets navigation through the original
game menus. Preserve their original appearance and high-level structure while
making button presses respond immediately and adding a dedicated Back action.

## Agreed scope

- Responsive navigation becomes the default. Original menu behaviour remains
  selectable through a setting.
- Apply the improvement throughout the original menu journey, including mode
  selection, name entry, save/load and pre-race screens. Racing controls are
  a separate concern.
- Retain existing Back, Cancel and Exit menu entries alongside the dedicated
  Back action, preserving the original menu layout.

## Agreed interaction

- Controller B and keyboard Escape invoke Back. Backspace retains its text
  deletion role. Menu navigation is independent of the Original/Modern driving
  control preset.
- In responsive mode, selection, confirmation and screen changes happen at the
  next input/presentation opportunity. Remove deliberate input waits and blocking
  palette fades; retain the original screen artwork and layout. Original mode
  retains its existing timing.
- A direction moves immediately, repeats after 300 ms held, then repeats every
  100 ms. Separate taps respond immediately. Confirm and Back require a fresh
  press and cannot cascade through screens when held.
- Directions held across a screen change require release before moving the new
  screen's selection. Apply the same protection after Settings, lost focus and
  controller reconnection. A fresh press remains immediately responsive.
- Back returns one logical level and restores the previous selection. Existing
  Cancel entries follow the same route. In Practice, cancelling track selection
  returns to division selection instead of jumping to the main menu.

## Back routes in responsive mode

| Screen | Back action |
| --- | --- |
| Mode chooser or main menu | None; these are root screens. Closing the window remains the way to quit. |
| First player's name | Return to mode chooser. |
| Additional player's name | Return to Add Player; remove an unfinished new player slot. |
| Add Player | Edit the most recently completed name. |
| Opening roster/standings | Return to the last setup step: first name for single player, Add Player for multiplayer, or eighth name when setup advanced automatically. |
| Practice division | Return to main menu. |
| Practice track | Return to division selection, including the existing Cancel row. |
| Practice preview | Return to track selection. |
| Championship preview | Return to the prepared matchup. |
| Championship matchup | None; the season/fixture is already prepared. |
| Championship results, records, standings, awards and concluding roster | None; retain forward progression through A/Enter. |
| Hall of Fame | Close to main menu. |
| Load/Save menu | Return to main menu. |
| Save/load slot list | Return to Load/Save menu. |
| Save-name editor | Return to slot list, discarding the unconfirmed edit. |
| Completed save/load return screens | Dismiss the completion sequence; the operation remains committed. |

Retain entered player names while moving between setup screens, including when
editing a previously completed player. Editing a completed player does not add
or remove a player. Cancelling a newly added, unfinished player removes only
that pending slot. Existing name validation and player-count limits remain.

Save/load Back cancels unfinished work one step at a time. A successful save or
load is committed and cannot be undone through Back. Error/warning prompts
follow their unfinished operation's cancellation route. Existing Settings and
Pause cancellation retain their own Apply/Cancel and resume behaviour.

The baseline main menu does not return to mode selection or end the session. No new
quit confirmation or return-to-launcher flow is part of this improvement.

## Agreed setting

Expose Menu navigation: Responsive / Original in both the launcher and in-game
Settings, saved independently of driving controls. Existing preferences adopt
Responsive. Restore original defaults selects Original. Applying the setting
preserves the current session and screen.

Original navigation retains the existing timing, routes and cancellation
behaviour. It is distinct from the Original driving preset. Responsive
navigation changes neither the selected driving preset nor other preferences.

## State and fidelity boundaries

Back navigates or cancels; it never undoes race outcomes, skips fixtures, awards
points twice or reruns promotion/relegation. Informational championship pages
retain their existing progression effects, performed once at the same logical
transition. Their A/Enter continuation gains responsive timing.

Revisiting a prepared preview must reuse the prepared race instead of running
track/fixture preparation again. A different deliberate track/race choice still
performs its normal preparation. Merely restoring a screen or changing the
navigation setting must not reset gameplay, repeat save encoding, consume
additional gameplay RNG or replay competition progression. Preserve the
existing effects of deliberate actions and save transactions.

Keep the original fixed-point arithmetic, race rules, RNG update order and
gameplay/audio clocks. Input response is measured against the next available
host input/presentation opportunity, not the 120 ms gameplay tick. Direction
repeat uses elapsed host time. Removing intentional waits does not guarantee
that disk access or race construction completes within one frame.

The implementation must preserve a separately verifiable Original path and
share existing gameplay transitions without introducing oracle code into the
player. Original rendering, numeric selection, text rules and driving bindings
continue to apply except for the explicitly agreed navigation changes.

## Acceptance and verification

- Exercise short taps, rapid separate taps, direction changes, repeat timing
  and confirm/Back holds. Ready screens respond on the next input/presentation
  opportunity without a deliberate debounce or transition wait.
- Check every Back route and equivalent Cancel row, restored selections, both
  root screens, name editing, pending-player cancellation, and two-to-eight-player
  setup including the automatic eighth-player transition.
- Check layered save/load cancellation, committed operations, error prompts,
  preference migration, Apply/Cancel, Restore original defaults and switching
  navigation policy without rebuilding the session.
- Verify preview revisits and redraws do not repeat preparation or consume extra
  gameplay RNG. Verify championship continuation scores, bonuses, round/driver
  advancement and season endings occur once with the original rules.
- Retain original-code comparisons for Original navigation. Add focused native
  unit/integration checks for responsive input and route/state boundaries; use
  CMake presets and CTest plus the game-flow wrapper selected in
  [development](../development.md#choose-verification-by-change).
- Manually inspect keyboard/controller response, held input at screen changes,
  focus/device recovery, Settings changes, original artwork/layout, and both
  navigation policies. Stage an implemented player using CMake Install.

All product branches discussed in the design interview are settled. Keep this
contract alongside the component guides when evolving the implementation.

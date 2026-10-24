# Native Game menu

The native Game menu provides keyboard/controller access to Settings and
application exit in both windowed and fullscreen play. It is separate from the
original menu artwork and competition rules.

## Input routes

| Context | Escape | Controller Start / P |
| --- | --- | --- |
| Race, including an unacknowledged finish | Open Game menu | Open Game menu |
| Top-level mode chooser | Open Game menu | Open Game menu |
| Nested original menu | Existing Back/Cancel route | Open Game menu |
| Player/save-name editor | Existing Back/Cancel route | Start opens Game menu; P types normally |
| Game menu | Resume/return | Resume/return |
| Settings | Cancel to its caller | Cancel to its caller |
| Quit confirmation | Cancel to Game menu | Cancel to Game menu |

Controller B retains Back/Cancel. F10 directly opens Settings, except during
Quit confirmation. P remains an alternate shortcut; name entry owns its letters.
Menu opening and page changes require fresh confirmation/back presses. Holding
Enter/A across Quit selection cannot activate the following confirmation.

## Actions and persistence

- Resume returns to the exact underlying screen and clock phase.
- Settings retains preview, Apply/Cancel and window-placement restoration.
- Leave Race appears only during an unfinished race. It resumes the session
  with a latched request through the existing race-exit path. Original exit
  gates, cleanup, damage and scoring rules remain authoritative. Controller
  Back/View retains its existing direct race-exit shortcut.
- Quit to Desktop opens a confirmation with Cancel first and selected. Career
  confirmation describes saving and, for an unfinished competitive race,
  restarting at its matchup on Resume Season. Multiplayer warns that progress
  since the last manual save will be lost.
- Confirmed exit shares the window-close/Alt+F4 save path. Career and Hall writes
  must flush successfully, or the existing Retry/Leave Without Saving/Cancel
  screen is shown. Cancelling that save decision reopens the Game menu so a
  paused race cannot resume unexpectedly.

Game menu, Settings and confirmation pages stop simulation/audio updates while
presentation continues. Focus loss/disconnection retains explicit resume. There
is no Return to Careers or return-to-launcher action in this menu; career
switching retains its original Back hierarchy.

Focused verification covers shortcut routing, conditional actions, held inputs,
confirmation cancellation, Settings return, paused rendering, original race-exit
equivalence, and save failure/cancel/retry. See [development](../development.md)
and [career persistence](career-persistence-design.md) for the retained contracts.

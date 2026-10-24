# Single-player career persistence design

Status: accepted and implemented; verification is described in
[career persistence](../components/native-career-persistence.md).

## Agreed scope

Persistent single-player careers, Continue, autosave between races and automatic
Hall of Fame persistence form this delivery. Multiplayer autosave is a later
extension; preserve the existing multiplayer game apart from replacing legacy
HALL transport with automatic shared records.

Retain original racing rules, arithmetic, RNG behaviour and simulation clocks.
The existing save encoder consumes gameplay RNG, so persistence must capture
state without invoking that encoder on the live game. The legacy career payload
omits round/fixture progress and standings; a native continuation checkpoint
needs more than that payload.

## Design tree

Career identity, menus, creation/selection/deletion, Back and continuation,
checkpoint/record durability, Practice, write failures, recovery, save feedback
and instance ownership are settled. Legacy import, Rename, single-player manual
save/load/Replay, mid-race driving resume and multiplayer autosave are excluded.
No product questions remain; the shared understanding was confirmed before building.

## Agreed foundations

- A career is a persistent single-player driver identity and its progress across
  seasons. Multiple careers may use the same driver name.
- Career selection always follows Single Player in the existing mode chooser,
  using original artwork and fonts. New Career always appears. Continue appears
  when the last-used career is available; Browse Careers appears when saved
  careers exist. With none, New Career is the only row. Only New Career enters
  name setup. The launcher remains for setup/settings.
- Continue and Browse Careers open the selected driver's three-row Single Player
  main menu: Hall of Fame, Practise and Start Season/Resume Season. There is no
  fourth Career option; Back returns to career selection.
- Each Windows user has one automatically loaded Hall of Fame, shared across
  careers and local multiplayer. Removing a career does not remove its records.
  Preserve original holder names and eligibility rules.
- Autosave is always active for these careers, independently of both the
  navigation policy and driving preset.

## Agreed creation and continuation

- Create the first career autosave after confirming name entry and the opening
  roster, before the first race. New Career adds a separate career without
  replacing an existing one.
- Continue opens the last-used career's main menu. Browse Careers displays driver name,
  division/league and last played to distinguish careers with the same name.
- The career list has no fixed thirty-slot limit, supports scrolling/paging,
  and is ordered by last played. Entry initially selects Continue when present,
  otherwise New Career. Unavailable actions are hidden, not dead-end rows.
- Closing or crashing during an unfinished race restores that fixture's
  pre-race checkpoint, including pre-race damage and state. Deliberate Leave
  Race retains the original quit and scoring rules. Resume Season reopens the matchup
  so the player can proceed through the preview when ready; it does not start
  the car immediately. Restoring that matchup must preserve preparation state
  and must not repeat prior RNG/track setup inadvertently.
- A completed race becomes durable when its completion message appears, before
  acknowledgement. Resume Season restores outstanding finish/results/award screens;
  scoring and progression are applied exactly once.

## Agreed Hall of Fame durability

Persist qualifying competitive records when earned, independently of career
checkpoint timing. A completed-lap record survives closing the application
later in that unfinished race, even though Resume Season restarts the fixture.
Practice and incomplete laps do not gain eligibility. Preserve the original
record-holder and eligibility rules.

The new shared Hall is authoritative in both single-player and multiplayer.
Retire legacy HALL save/load commands in both modes; multiplayer progress
saving remains manual. Do not implicitly import legacy records through a
multiplayer session's record banks.

## Excluded legacy import

Do not implement backward-compatible import or migration of original career
slots or HALL saves in this delivery. Existing `.scr-save` files are not inputs
to the new career/Hall stores and remain untouched. Multiplayer autosave remains
outside scope; the existing multiplayer game is retained, with automatic shared
Hall persistence replacing legacy HALL transport.

## Agreed career management and menus

- Remove the single-player Load/Save/Replay row. The selected career's main menu
  has three rows and no Career shortcut. The third row changes from Start Season
  to Resume Season while a season is pending. Original navigation does not select
  a different persistence model.
- Browse Careers supports Delete with confirmation; it does not support Rename.
  Deleting a career leaves the shared Hall of Fame intact. Original name-entry
  rules remain applicable when creating a career.
- Multiplayer retains its existing progression/manual-save flow; multiplayer
  autosave is not part of this delivery.

## Agreed Practice continuation

Continue always opens the selected career's main menu, including after Practice.
Normal interactive Practice exit still returns to track selection. Keep the
career's progress and retain the resulting RNG state on normal exit; Practice
does not create season progress or Hall records. Do not recover a normally
completed Practice by rewinding its main-menu RNG to an older state. If a season
is suspended, keep its continuation and RNG separate and unchanged throughout
Practice. Resume Season restores that exact season even after normal Practice
exit, interrupted Practice or application restart.

## Agreed Back routes and season suspension

| Current screen | Back destination |
| --- | --- |
| Competitive track preview | Prepared matchup |
| Matchup | Single Player main menu |
| Finish/results/award screen | Single Player main menu |
| Single Player main menu | Career selection |
| Career selection | Original mode chooser |

Back suspends the season at its current continuation stage. Resume Season returns
to the same matchup or outstanding finish/report phase. Continue opens the
career's main menu. Back does not forfeit, undo outcomes, acknowledge a report or
start another season. Career selection provides New Career and Browse Careers
without discarding a saved season. Apply these routes independently of navigation timing policy;
multiplayer retains its existing routes.

While driving, Escape opens the native [Game menu](game-menu-design.md).
Its Leave Race action retains the original exit gates and scoring consequences;
Quit to Desktop uses the checkpoint-aware application-close path. Do not add a
Return to Careers command to this menu. Start/P also opens the Game menu, and
F10 retains direct Settings access outside Quit confirmation.

Career selection is the parent of career creation and each selected career's
main menu; its own Back returns to the mode chooser. Keep setup drafts separate from a
created career, retaining the original name/roster editing journey. Cancelling
setup before its first successful save does not register a career.

Deleting a career removes its current autosave and its recovery copies after
confirmation, without changing the shared Hall. It cannot be resurrected by
backup discovery or pending writes. If the last-used career is deleted or
unavailable, require an explicit remaining-career choice rather than silently
continuing a different driver's career. Deletion does not start a new career
automatically.

## Agreed save feedback

Show small Saving... / Saved status in menus and pause screens, retaining the
original visual style, with a persistent Not saved warning after a write failure.
Successful Hall writes do not add driving popups. A pending write is not reported
as Saved; recovery can only restore successfully committed data.

## Agreed write failures and recovery

- Retain live play state and the last good save on a failed write. Clearly show
  that progress is unsaved, and offer Retry / Continue Without Saving at a safe
  menu. A Hall record write failure while driving defers the prompt until the
  race ends. Continue attempting autosaves at subsequent checkpoints; report
  success only after the write completes.
- After Continue Without Saving, Back, Continue and Resume Season retain the
  active career's in-memory season rather than reloading an older
  disk checkpoint. Keep the warning and retry automatically without repeating
  the same prompt at every checkpoint.
- Before changing careers or normal application closure, wait for outstanding
  writes. If saving still fails, offer Retry / Leave Unsaved / Cancel before
  discarding live progress. Distinguish career-progress and record-write failures.
- If the first career save fails, creation stops with Retry / Cancel. Do not
  start a career that has never been saved. Continue Without Saving applies
  to an existing career's later write failures.
- Keep the current autosave and two previous valid copies per career. Apply
  equivalent protection to the shared Hall of Fame. Recover the newest valid
  backup if the current file is damaged, and clearly report recovery because
  an older checkpoint may lose recent progress.
- If no valid recovery copy exists, preserve the unreadable career files and
  make that career unavailable; allow choosing another, creating a new career,
  or explicitly deleting the damaged one. An unrecoverable Hall offers an
  explicit fresh start while preserving damaged files. Never silently reset
  progress/records, and leave unsupported save versions untouched.
- Only one running game instance may own a save-data location. A second launch
  using that location brings the existing window forward instead of creating
  conflicting writers. Different data locations remain separate.

## Recovered constraints

Competitive race exit transfers record candidates into the session Hall banks;
Practice does not. A completed competitive lap can remain eligible on a later
race quit; a race-time record needs three completed laps. The agreed enhancement
makes eligible live-race records durable before exit without promoting Practice
candidates or applying career race-exit/scoring effects early to the live game.

Finishing a race inside the driving scene is distinct from committing its exit
to session state and scoring it. Season promotion/relegation and the final
table reset are also separate transitions. Resume Season must distinguish committed
phases rather than re-running Start Season, scoring or awards on restoration.

Practice preserves career division, driver order, league and pre-race damage,
and does not score a season or transfer Hall records. Its normal exit does keep
the resulting RNG state and returns to track selection. Practice runs from the
selected career's main menu, with a suspended season retained separately so its
state and RNG cannot be changed by Practice.

Existing Pause and Settings can also cover a completed race's unacknowledged
end message. Leaving a paused session must distinguish that durable completion
from an unfinished race eligible for pre-race restart.

## Architecture and verification boundary

Use a versioned native continuation format with explicit career identity and
progression stage; preserve both live and saved race RNG. Capture and restore
without running original save encoding, season initialization, race exit,
scoring, promotion or reset a second time. Assets still come from the validated
pack. A checkpoint is game continuation data, not an emulator capture.

The shared Hall is independent of career checkpoints and recovery. Restoring an
older career must not roll records back. Persist eligible human records only,
merge strictly better times, and preserve the current holder on ties. Browsing
the career list and Hall writes do not change which career was last used;
successfully opening or creating a career does.

Keep display, driving and navigation preferences shared; opening a career does
not replace them. Use the established per-user data location with isolated
development/test overrides. Validate saved data and versions before applying
state, and never overwrite an unsupported file just because it cannot be read.
Writes must replace committed files atomically; interrupted or failed replacement
must leave a usable prior generation. Saving and record observation must not
stall active driving or mutate simulation, render history or RNG.

The architectural decision is recorded in
[native career checkpoints and shared records](../adr/0001-native-career-checkpoints.md).

## Acceptance and verification

- Exercise creation/cancellation, repeated driver names, selection/list paging,
  no-career and unavailable-last-used cases, Delete/Cancel, and deletion of the
  current autosave plus recovery copies without later resurrection.
- Save/restore every continuation boundary: fresh career, season preparation,
  matchup and preview, interrupted race, completed-but-unacknowledged finish,
  each result/record/standings page, computer-only fixtures, promotion/relegation,
  Super League entry/victory, final roster/reset and return to the career menu.
- Compare restored and uninterrupted native runs under identical inputs and
  shared-record context, including race-start state, damage, scores, fixtures,
  live and saved RNG, subsequent physics, awards and next-season progression.
  Repeat capture and rendering queries to prove they do not change the outcome.
- Check Back suspension, main-menu Continue and Resume Season for every agreed route, including both
  navigation policies, both driving presets, paused terminal outcomes and fresh
  input after transitions. Keep Leave Race distinct from backing out of menus.
- Verify normal Practice exit retains career fields and its main-menu RNG,
  Continue returns to the main menu, and Practice never adds shared records.
  Preserve a suspended season's exact matchup/finish/report and RNG through
  Practice, interruption, career switching and relaunch.
- Check immediate eligible human lap/three-lap records, slower/tied results,
  partial laps, Practice, competitive quit and application closure mid-race.
  Verify record persistence is independent of career writes/deletion/recovery,
  older checkpoints cannot roll it back, and MP legacy HALL cannot import data.
- Inject failed/delayed writes and interrupted replacements; exercise first-save
  Retry/Cancel, existing-career Continue Without Saving, in-memory Continue,
  departure Retry/Leave Unsaved/Cancel, pending-write completion, backup rotation,
  corrupt/unsupported data, all-backups-unusable and explicit Hall fresh start.
- Verify one instance per data location and isolated development/test stores.
  Check normal startup has no dependency on legacy `.scr-save` import and does
  not alter old files; retain multiplayer manual progress-save regressions.
- Use CMake presets and CTest, the focused game-flow/persistence wrapper and
  relevant original-code rule comparisons selected by
  [development](../development.md#choose-verification-by-change). New continuation
  needs its own native roundtrip tests; old legacy-save passes do not prove it.
- Manually inspect original-style menus, keyboard/controller Back and Continue,
  name entry, paging, deletion, save feedback, failure prompts and return after
  relaunch. Record source/fixture/executable identities with results under
  `analysis/`, update current player/component documentation, and stage the
  verified player with CMake Install. Keep generated saves/assets out of Git.

## Existing contracts

- [Career state](../components/native-career-state.md)
- [Save codec and Replay](../components/native-save-state.md)
- [Local save storage](../components/native-save-store.md)
- [Menu navigation](menu-navigation-design.md)
- [Hall record state](../components/native-hall-records.md)

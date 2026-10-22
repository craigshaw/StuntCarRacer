# Complete-lap replay evidence

The replay wrapper can require a complete observed lap with
`--require-complete-lap`. The retained authored regression completes a full lap
from the existing Practice capture:

```powershell
. .\scripts\env.ps1
& $AmigaPython .\scripts\check_driving_replay.py `
  --inputs .\tests\replays\laps\authored-lap-506.csv `
  --full-loop --require-complete-lap --native-only-verify `
  --output .\analysis\driving-replay\complete-lap\check-results.json
```

The 506-tick regression passed against the actual original instructions with
zero mismatches, and its separate native-only trace agreed. It crosses the lap
marker at ticks 24 and 505, traverses all 44 forward segment boundaries between
them, disarms the crossing latch at tick 272, and performs no restart.

A longer regression completes three laps and reaches the recovered race-end
gate. It also passed the original/native comparison and separate native-only
trace comparison with zero mismatches:

```powershell
& $AmigaPython .\scripts\check_driving_replay.py `
  --inputs .\tests\replays\laps\authored-race-1463.csv `
  --full-loop --require-complete-lap --native-only-verify `
  --output .\analysis\driving-replay\complete-race\check-results.json
```

The 1,463-tick trace crosses at ticks 24, 505, 961 and 1,415. Each consecutive
pair encloses a certified full track cycle. The race-completion byte becomes
four at tick 1,415, and `outer_action` becomes `raceEnd` (2) at tick 1,462.
There are no restarts. The result artifact is
`analysis/driving-replay/complete-race/check-results.json`.

Both regressions contain authored inputs chosen by a native feedback experiment.
It evaluated short steering rollouts and selected accelerate/brake/steering
masks. Its live state advanced only through the recovered gameplay stages;
speculative state never replaced live state. The retained CSVs contain controls
only. Their adjacent provenance JSON files record input, capture and experiment
hashes, method and observed crossing ticks. They are stored below `laps/` so the
default short-scenario glob does not silently add them to every run. They are
not human recordings and do not establish real interrupt timing.

The existing short scenarios are also authored controls. The racing snapshot starts in Practice
at segment 15 of 44 with lap counter zero. Its lap marker is segment 16; its
crossing latch disarms at segment 37. Reaching counter one starts the clock.
It does not establish that a full lap was driven during the replay.

`scripts/replay_lap.py` reads these markers directly from the starting Chip RAM
using the same documented addresses as `src/state/race_timing_adapter.h`. It
requires all of the following before reporting a complete lap:

- Two observed counter increments at the lap marker, with the original
  crossing conditions and latch state.
- All track segments traversed in forward cyclic order between those events.
- An observed latch disarm at the track's finish-line segment.
- No restart between those events.

A captured partial lap is excluded. A skipped segment, reverse segment order,
or restart interrupts that candidate; a later complete cycle can still qualify.
This is a conservative progress certificate. The original game's counter and
the complete-lap certificate are reported separately.

The trace includes the lap counter, crossing latch, race-completion byte,
outer-loop action and cumulative restart count. The summary retains observed
crossing ticks, race-completion changes, restarts and interrupted candidates.
The original/native oracle establishes the compared state; the lap validator
establishes the progress demonstrated by that trace. Inspecting a CSV alone
does not establish that original code ran or that the inputs came from a human.

For a trace already checked by the replay wrapper:

```powershell
& $AmigaPython .\scripts\replay_lap.py inspect `
  .\analysis\driving-replay\00-lap-trace.csv `
  .\analysis\captures\race\chip-000000.bin --require-complete-lap
```

## Compact control timelines

Long inputs can be authored as a JSON duration/mask timeline and expanded into
the runner's existing `tick,controls` CSV. The following is only a syntax
example; its provenance explicitly identifies authored input:

```json
{
  "provenance": "Authored control experiment; no human input recording.",
  "steps": [
    {"ticks": 64, "controls": 1},
    {"ticks": 448, "controls": 0}
  ]
}
```

Save it beneath `analysis/replay-inputs/`, then expand it to a new output path:

```powershell
& $AmigaPython .\scripts\replay_lap.py expand `
  .\analysis\replay-inputs\timeline.json .\analysis\replay-inputs\lap.csv
```

The expander accepts duration and control-mask fields plus an optional `quit`
field containing 0 or 1. Including `quit` in any step produces the extended
`tick,controls,quit` schema; other steps default to released. Quit inputs require
the wrapper's `--full-loop` option and the timeline must end at the terminal
transition. The expander rejects position,
velocity, segment and other state fields, bounds masks to 0..31, and refuses to
replace an existing output. Its JSON result retains the provenance text and
SHA-256 hashes of the timeline and expanded input. Save that result beside the
inputs when retaining an experiment. Merely naming an input `lap.csv` does not
certify a lap; the wrapper must observe the complete cycle.

The evidence checks themselves are tested with synthetic traces, including
partial captures, counter-only false positives, shortcuts, reverse movement,
off-track crossings and restart discontinuities:

```powershell
& $AmigaPython .\tests\unit\test_replay_lap.py
& $AmigaPython .\tests\unit\test_driving_replay_wrapper.py
```

Those synthetic trace tests exercise the validator. They are not game execution
or a completed-lap demonstration.

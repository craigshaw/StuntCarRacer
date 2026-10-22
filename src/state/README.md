# Recovered state adapters

These header-only adapters translate between the recovered byte/word layout and
the typed gameplay/rendering APIs. `native_race.h` retains native stage ordering;
`replay_io.h` provides the shared replay and trace format. No original CPU executes
this memory representation.

Production uses `scr::state` (and `scr::state::memory` for the base layout helpers).
Tests import the same implementations through `tests/support/`, which owns oracle
code ranges and comparison projections. Keep test setup and emulator calls there
or in `tests/oracle/`; production code must not include test headers.

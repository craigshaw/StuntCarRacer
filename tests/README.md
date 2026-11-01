# Verification sources

- `unit/`: asset-free native checks and synthetic Python evidence/policy tests.
- `integration/`: the native game, startup, sessions, display, launcher and rendering.
- `oracle/`: comparisons against original 68000 code; built only with `SCR_BUILD_ORACLE`.
- `support/`: test-only namespaces, oracle code ranges, comparison projections and
  temporary-output helpers. Shared runtime adapters belong in `src/state/`.
- `replays/`: authored control sequences and their provenance, retained unchanged.

Use the CMake/CTest presets and [development guide](../docs/development.md) to
choose checks. Private fixtures and retained receipts stay at their existing
`analysis/` paths; disposable test output belongs under `build/`.

Hall coverage separates pure ranking/codec tests, the Windows merge/recovery
store, and accepted-completion delivery. `hall_completion_tests` drives the native
timing routine through real accepted events, including overflow and partial-entry
boundaries. `native_hall_completion_tests` exercises player ingestion with authored
captured-entry trajectories, private PB independence, local drivers, failed writes
and full native memory/RNG isolation. Injected old record banks are tested only
as excluded imports, never described as newly driven completions. See the
[shared Hall contract](../docs/components/shared-hall-of-fame.md).

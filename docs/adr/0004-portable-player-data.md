---
status: accepted
---

# Portable player data and native multiplayer continuation

Assets, settings, careers, multiplayer games and both Hall categories belong in
`data/` beside the executable. Everyone using the folder shares its data; moving
it carries the game, and independent copies stay independent. This supersedes
[ADR 0003](0003-per-user-player-data.md). Straightforward ownership/copying takes
priority over implicit Windows profile storage. There is no marker or fallback;
`--data-dir` remains a complete development/special-installation override.

Each career/named multiplayer game has a self-contained folder discovered from
its conventional checkpoint filename. Careers include private PBs, ghosts and
recoveries. A folder identifies the copy selected for loading/autosaving; internal
IDs bind its contents. Matching IDs in separate folders are allowed without
rewriting either. Remember last-used folders separately in settings. Renaming a
folder may require choosing it again, but cannot invalidate its saved contents.

File operations belong to the operating system. The launcher/game provide no
saved-data import, conversion, conflict-resolution or management UI/backend.
Browse opens a selected game directly. Old layouts and original multiplayer slots
have no migration support. Retain atomic writes, bounded recovery generations and
existing failure handling. The Hall and simulation/RNG isolation from
[ADR 0001](0001-native-career-checkpoints.md) remain independent of copied progress.

Native multiplayer autosave/Resume retain complete competition state. New Game
selects every division and Normal/Super League independently of careers/access
files, using the original two-track championships, scoring and turn rules.
Single-player progression remains unchanged; Computer Link stays deferred.

[The implementation plan](../components/portable-player-data.md) records completed
foundations and pending stage 6 acceptance for the simplified design.

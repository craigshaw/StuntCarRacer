---
status: accepted
---

# Portable player data and native multiplayer continuation

Player data will belong to one game folder: assets, settings, careers, multiplayer
games and the Hall of Fame live under `data/` beside the executable. Everyone
using that folder shares its data; moving the folder carries the complete game,
and separate copies are independent. This supersedes
[ADR 0003](0003-per-user-player-data.md): straightforward copying and ownership
take priority over supporting unwritable installation folders through implicit
Windows application-data storage. Portable storage is the default, without a
marker directory or silent fallback; an explicit data-root override remains for
development and special installations.

Each career or named multiplayer game has a self-contained folder discovered
from its contents, with a stable internal identity independent of its name or
path. Careers include their personal bests, ghosts and recovery copies. Native
multiplayer autosave and Resume replace the normal virtual save-disc flow and
retain complete competition state. Copy conflicts offer Replace, Keep Both or
Cancel; progress is never automatically merged. The independent Hall and the
simulation/RNG isolation established by [ADR 0001](0001-native-career-checkpoints.md)
remain in force.

Multiplayer New Game offers every division and Normal/Super League directly,
with the selected original two-track championship configuration owned by that
saved game. This removes dependency on single-player unlocks or access files;
single-player progression and original multiplayer scoring remain unchanged.
Legacy thirty-slot multiplayer saves are excluded from compatibility and migration:
there is no requirement for an importer or dedicated preservation/archive step.
Existing native career, PB and Hall compatibility is retained.

Portable defaults and migration remain pending. Stages 1–4 provide explicit
data-root resolution, process-held ownership, self-contained career folders and
named native multiplayer autosave/Resume, with shared copy resolution and
recoverable folder transactions. See the [design and staged implementation plan](../components/portable-player-data.md)
for delivery status, compatibility boundaries, migration and verification.

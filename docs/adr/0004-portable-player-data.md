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

This is an accepted design, not an implemented runtime change. See the
[design and staged implementation plan](../components/portable-player-data.md)
for compatibility boundaries, migration and verification.

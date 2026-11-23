---
status: superseded by ADR-0004
---

# Store player data in per-user Windows application data

[ADR 0004](0004-portable-player-data.md) replaces this decision with portable
game-folder ownership. The implementation still uses the paths below until the
[portable-data plan](../components/portable-player-data.md) is delivered.

Normal launches store imported assets, preferences and saves in per-user Windows
application data so the application also works from a read-only installation
folder. This provides one consistent location across executable copies.
Development commands retain explicit overrides for checkout-local assets and
working data. See [player paths](../playing.md#data-and-recovery).

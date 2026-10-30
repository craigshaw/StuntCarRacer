---
status: accepted
---

# Native career checkpoints and an independent Hall of Fame

Single-player continuation uses versioned native checkpoints with a persistent
career identity and explicit progression stage: the original save payload omits
in-progress season state and its encoder advances gameplay RNG. Capture and
restoration therefore preserve gameplay state without invoking original save or
progression handlers as a side effect. The per-user Hall of Fame is stored and
merged independently so restoring or deleting a career cannot roll back earned
records. Importing original manual-slot career/HALL files is unsupported.

Opening a career lands at its main menu. Resume Season owns exact season
continuation, independently of the menu/Practice state, so a player can practise
without changing a pending fixture, report or season RNG. A bounded optional
season checkpoint inside the main-menu checkpoint preserves both across launches.

See the [session and persistence contract](../components/sessions.md).

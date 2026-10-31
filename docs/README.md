# Documentation

| Start here | Purpose |
| --- | --- |
| [Playing](playing.md) | Setup, controls, careers, multiplayer, settings and recovery |
| [Development](development.md) | Build, test, install and choose verification |
| [Architecture](architecture.md) | Ownership, clocks and fidelity boundaries |
| [Glossary](../GLOSSARY.md) | Project terminology |

[AGENTS.md](../AGENTS.md) records contributor constraints.

## Maintainer references

| Area | What must survive an extension |
| --- | --- |
| [Assets](components/assets.md) | Supported disk, import and initialization order |
| [Simulation](components/simulation.md) | Arithmetic, RNG, stage order and stateful drawing inputs |
| [Rendering and audio](components/presentation.md) | Snapshots, aspect modes, compositing and host boundaries |
| [Sessions and persistence](components/sessions.md) | Progression, checkpoints, shared records and durable writes |

## Architectural decisions

- [0001 — Native career checkpoints and independent Hall](adr/0001-native-career-checkpoints.md).
- [0002 — Application-owned asset import](adr/0002-native-application-owns-asset-import.md).
- [0003 — Per-user player data](adr/0003-per-user-player-data.md).

## Proposed enhancements

- [Personal-best lap ghosts](components/personal-best-ghosts.md): recording in
  Practice and career league races, Practice playback, rendering/Draw Bridge proof
  gates and a future online extension. Rendering proofs and in-memory recording
  are available; durable storage and player controls remain pending.

## Tools and research

[Scripts](../scripts/README.md), [viewer controls](../tools/README.md),
[test layout](../tests/README.md) and [local configuration](../setup/README.md)
cover everyday work. [Research reproduction](research/reverse-engineering.md)
and the [evidence map](research/evidence.md) locate private inputs and identified
historical reports. Current passing results require fresh verification receipts.

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
- [0003 — Per-user player data](adr/0003-per-user-player-data.md), superseded as
  a design decision by ADR 0004; retained as historical context.
- [0004 — Portable player data and native multiplayer continuation](adr/0004-portable-player-data.md).

## Implementation plans

- [Portable player data and multiplayer saves](components/portable-player-data.md):
  agreed ownership and folder structure, native multiplayer autosave/Resume,
  copying, recovery and the staged implementation plan. Portable defaults,
  self-contained career folders and named multiplayer autosave/Resume are
  implemented. Convention-based loading replaces import/conversion and
  file-management UI. All six stages are complete, with automated release/portable
  checks and maintainer manual sign-off.

## Optional features

- [Shared Hall storage](components/shared-hall-of-fame.md): bounded rankings,
  retained ghosts, completion delivery, career-menu browser, isolated Practice challenges and recovery.
- [Personal-best lap ghosts](components/personal-best-ghosts.md): recording in
  Practice and career league races, Practice playback, rendering/Draw Bridge proof
  gates, saved targets and player controls. A future online extension remains
  separate from the local feature.
- [Enhanced timing implementation plan](components/enhanced-timing.md): approved
  design for millisecond records, interpolated crossings and explicit recovery
  penalties alongside Original timing. All six stages are complete, including
  separate records/targets, player settings/timer/Hall UI and scoped final
  release verification and staging.

## Tools and research

[Scripts](../scripts/README.md), [viewer controls](../tools/README.md),
[test layout](../tests/README.md) and [local configuration](../setup/README.md)
cover everyday work. [Research reproduction](research/reverse-engineering.md)
and the [evidence map](research/evidence.md) locate private inputs and identified
historical reports. Current passing results require fresh verification receipts.

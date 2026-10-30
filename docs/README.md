# Documentation

| Start here | Purpose |
| --- | --- |
| [Playing](playing.md) | Setup, controls, careers, multiplayer, settings and recovery |
| [Development](development.md) | Build/test/install, tool/fixture setup and identified verification |
| [Architecture](architecture.md) | Runtime ownership, independent clocks and fidelity boundaries |
| [Glossary](../GLOSSARY.md) | Project terminology |

[AGENTS.md](../AGENTS.md) records contributor constraints. Open the component
matching the change rather than reading every guide. Component test counts refer
to recorded scenarios; current verification requires matching source, fixture
and executable receipts under ignored `analysis/`.

## Game flow and storage

| Area | Contracts |
| --- | --- |
| Setup/assets | [Launcher](components/native-launcher.md), [pack/import](components/native-startup.md), [asset initialization](components/native-asset-initialization.md), [race initialization](components/native-race-initialization.md) |
| Opening | [Mode selection, name input and screens](components/native-opening.md) |
| Menus | [Flow/navigation](components/native-menu-flow.md), [pixels](components/native-menu-rendering.md), [preview](components/native-track-preview.md), [palette fades](components/native-palette-fade.md) |
| Careers | [Original rules/screens](components/native-career-state.md), [native continuation and persistence](components/native-career-persistence.md) |
| Multiplayer | [Championship rules and screens](components/native-multiplayer-state.md) |
| Hall of Fame | [Record state and rendering](components/native-hall-of-fame.md) |
| Manual saves | [Codec/Replay](components/native-save-state.md), [save UI](components/native-save-menu.md), [host storage](components/native-save-store.md) |

## Simulation and presentation

- Driving: [headless replay](components/driving-replay.md), [complete-lap evidence](components/replay-lap-validation.md),
  [opponent AI](components/opponent-driving.md), [opponent races](components/native-opponent-races.md),
  [opponent rendering](components/opponent-car.md), [Draw Bridge](components/draw-bridge.md).
- Cockpit: [instruments](components/native-cockpit-instruments.md), [timing](components/cockpit-timing.md),
  [effects](components/native-cockpit-effects.md), [chains](components/native-cockpit-chains.md),
  [damage](components/native-damage-holes.md), [messages](components/native-race-messages.md),
  [particles](components/native-driving-particles.md), [wheels](components/wheel-animation.md),
  [audio](components/native-audio.md).
- Rendering: [presentation/Game menu](components/native-presentation.md), [scene commands](components/native-scene-commands.md),
  [aspect views](components/native-extended-view.md), [sides/mountains](components/native-sides-and-mountains.md),
  [near track](components/native-near-track.md), [bounded audit](components/native-render-audit.md).
- Development viewers: [direct driving](components/native-driving.md), [recorded replay](components/native-replay-viewer.md).
  Their controls differ from the player application.

## Architectural decisions

- [0001 — Native career checkpoints and independent Hall](adr/0001-native-career-checkpoints.md).
- [0002 — Application-owned asset import](adr/0002-native-application-owns-asset-import.md).
- [0003 — Per-user player data](adr/0003-per-user-player-data.md).

## Tools and research

[Everyday commands](../scripts/README.md), [development tools](../tools/README.md),
[optional local configuration](../setup/README.md), [research setup/mapping](research/reverse-engineering.md)
and [retained evidence](research/evidence.md) cover reproduction without bundling private inputs.

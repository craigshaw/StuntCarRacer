# Documentation index

Start with the [project README](../README.md). For a new development thread, read
[AGENTS.md](../AGENTS.md), [development](development.md) and
[architecture](architecture.md), then open only the relevant component guides.

## Design and language

- [Launcher design](design/launcher-design.md): setup flow, agreed scope and UX decisions.
- [Menu navigation design](design/menu-navigation-design.md): responsive navigation, Back routes and preservation of Original navigation.
- [Glossary](../GLOSSARY.md): language for the native port and its setup experience.

## Run, build and boundaries

| Guide | Purpose |
| --- | --- |
| [Playing and display settings](playing.md) | Original routes, controls, saves and preferences |
| [Development and verification](development.md) | CMake presets, CTest, staging, focused checks and receipts |
| [Architecture](architecture.md) | Runtime ownership, clocks and fidelity contract |
| [Assets and startup](components/native-startup.md) | Local pack, import, format and Git safeguards |
| [Reverse engineering](research/reverse-engineering.md) | Toolchain, disk mapping, WinUAE and evidence |
| [Evidence map](research/evidence.md) | Original inputs, captures, references and identified verification reports |
| [Local tool configuration](../setup/README.md) | Optional per-machine paths and WSL settings |
| [Script directory](../scripts/README.md) | Everyday build/check/launch commands and research tooling |

## Game flow and persistence

- Opening: [flow](components/native-opening-flow.md), [name entry](components/native-player-name.md),
  [graphics](components/native-opening-render.md).
- Menus: [flow](components/native-menu-flow.md), [pixels](components/native-menu-rendering.md),
  [track preview](components/native-track-preview.md), [palette fades](components/native-palette-fade.md).
- Competition: [opponent races](components/native-opponent-races.md),
  [career state](components/native-career-state.md), [career screens](components/native-career-render.md),
  [multiplayer state](components/native-multiplayer-state.md),
  [multiplayer screens](components/native-multiplayer-render.md).
- Records and saves: [Hall of Fame](components/native-hall-of-fame.md),
  [record state](components/native-hall-records.md), [save encoding and Replay](components/native-save-state.md),
  [save UI](components/native-save-menu.md), [host storage](components/native-save-store.md).
- Fresh state: [asset initialization](components/native-asset-initialization.md),
  [race initialization](components/native-race-initialization.md).

## Simulation and presentation

- [Headless replay contract](components/driving-replay.md) and [complete-lap evidence](components/replay-lap-validation.md).
- [Opponent driving](components/opponent-driving.md), [opponent rendering](components/opponent-car.md),
  [Draw Bridge](components/draw-bridge.md).
- [Cockpit pixels](components/native-cockpit-instruments.md), [cockpit timing](components/cockpit-timing.md),
  [flames and visual audit](components/native-cockpit-effects.md), [lifting chains](components/native-cockpit-chains.md),
  [damage at race entry](components/native-damage-holes.md),
  [race messages](components/native-race-messages.md), [dust and sparks](components/native-driving-particles.md),
  [wheel animation](components/wheel-animation.md), [audio](components/native-audio.md).
- [Presentation API](components/native-presentation.md), [scene commands](components/native-scene-commands.md),
  [sidewalls and mountains](components/native-sides-and-mountains.md),
  [near-track visibility and edges](components/native-near-track.md),
  [bounded rendering audit](components/native-render-audit.md).
- Development viewers: [live driving](components/native-driving.md),
  [recorded replay](components/native-replay-viewer.md). Their controls differ from the game host.

Component guides retain recovered addresses, test seams and evidence IDs. Test
counts there describe specific recorded runs; use source/executable-identified
receipts for current verification, rather than treating prose as a test result.

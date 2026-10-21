# Documentation index

Start with the [project README](../README.md). For a new development thread, read
[AGENTS.md](../AGENTS.md), [development](development.md) and
[architecture](architecture.md), then open only the relevant component guides.

## Run, build and boundaries

| Guide | Purpose |
| --- | --- |
| [Playing and display settings](native-game.md) | Original routes, controls, saves and preferences |
| [Development and verification](development.md) | Builds, focused checks, receipts and handoff |
| [Architecture](architecture.md) | Runtime ownership, clocks and fidelity contract |
| [Assets and startup](native-startup.md) | Local pack, import, format and Git safeguards |
| [Reverse engineering](reverse-engineering.md) | Toolchain, disk mapping, WinUAE and evidence |

## Game flow and persistence

- Opening: [flow](native-opening-flow.md), [name entry](native-player-name.md),
  [graphics](native-opening-render.md).
- Menus: [flow](native-menu-flow.md), [pixels](native-menu-rendering.md),
  [track preview](native-track-preview.md), [palette fades](native-palette-fade.md).
- Competition: [opponent races](native-opponent-races.md),
  [career state](native-career-state.md), [career screens](native-career-render.md),
  [multiplayer state](native-multiplayer-state.md),
  [multiplayer screens](native-multiplayer-render.md).
- Records and saves: [Hall of Fame](native-hall-of-fame.md),
  [record state](native-hall-records.md), [save encoding and Replay](native-save-state.md),
  [save UI](native-save-menu.md), [host storage](native-save-store.md).
- Fresh state: [asset initialization](native-asset-initialization.md),
  [race initialization](native-race-initialization.md).

## Simulation and presentation

- [Headless replay contract](driving-replay.md) and [complete-lap evidence](replay-lap-validation.md).
- [Opponent driving](opponent-driving.md), [opponent rendering](opponent-car.md),
  [Draw Bridge](draw-bridge.md).
- [Cockpit pixels](native-cockpit-instruments.md), [cockpit timing](cockpit-timing.md),
  [race messages](native-race-messages.md), [dust and sparks](native-driving-particles.md),
  [wheel animation](wheel-animation.md), [audio](native-audio.md).
- [Presentation API](native-presentation.md), [scene commands](native-scene-commands.md),
  [sidewalls and mountains](native-sides-and-mountains.md),
  [near-track visibility and edges](native-near-track.md),
  [bounded rendering audit](native-render-audit.md).
- Development viewers: [live driving](native-driving.md),
  [recorded replay](native-replay-viewer.md). Their controls differ from the game host.

Component guides retain recovered addresses, test seams and evidence IDs. Test
counts there describe specific recorded runs; use source/executable-identified
receipts for current verification, rather than treating prose as a test result.

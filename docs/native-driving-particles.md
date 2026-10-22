# Driving particles

The native ground/edge effects stage retains its recovered gameplay state and
RNG sequence. Its optional `DrivingParticleDraws` output records accepted draw
calls in original order: existing particles before movement, then particles
drawn immediately after spawning. Final particle positions cannot substitute
for these events. Off-track dust uses 16 particles (four in the original
network mode); edge sparks use 32.

`app/native_driving_particles.*` presents these events using initialized masked
objects 29–36. Original tables at `0x60f9c` and `0x60fac` select each object's
horizontal offset and its 16-phase animation. The phase uses particle index
plus the gameplay frame counter. Drawing clips to the physical viewport
`x32..287, y16..143`, preserving the cockpit and damage strip.

Sparks reproduce the original three colour-3 pixels and upper-right colour-15
pixel. The original pixel helper retains its initial word pointer while moving
to the second column. Consequently, when local x ends in 15, that column wraps
to the beginning of the same 16-pixel word. The faithful renderer retains this
behaviour.

## Original-code evidence

The supported disk payload is SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`,
408540 bytes loaded at `0xe700`; its provenance is in
`analysis/map/manifest.json`. REA/Ghidra observations are corroborated by
Capstone's MC68000 decoding and execution in the test-only Musashi harness.

| Original range | Contract | REA evidence |
| --- | --- | --- |
| `0x60c92..0x60e86` | Ground/edge update, particle draw/move/spawn order and RNG | Executed by the particle and pre-existing effects oracles |
| `0x60e88..0x60f34` | Bounds validation and spark drawing | `ev_31d68dcaa623a7d49cc0dea93bff8eb52dc3b9cb186a4b63215e2d93daee921d` |
| `0x60f5a..0x60f9b` | Dust phase, placement and object selection | `ev_8e1c6bf980664d78b034fbd07d7b58985eeaa4771f42c7e0ee09f67e76128da3` |
| `0x6a0be..0x6a165`, calling `0x69fe8` | Shifted masked objects with viewport clipping | `ev_65765144a9f67b6577a0cdd220e478b38753aa4c41b6e1bf74e2ca7cd4866d65` |

The ignored export `analysis/rea/race-effects-evidence.json` retains the REA
records. These addresses refer to observed runtime mapping, not disk offsets.

`tests/oracle/native_driving_particles_tests.cpp` starts from the local asset pack,
checks the original executable ranges against the payload, and executes those
bytes. It compares every canvas pixel for animation, alignment, clipping,
overlap and spark colours. Full-stage sequences compare the ordered draw/audio
events and recovered state projection, including RNG, independently on each
side. Collecting draw events must produce the same state as an update without
event collection. View isolation and natural off-track drives are covered by
the startup/session tests. This bounded evidence does not claim universal
world-renderer pixel identity or Amiga hardware timing.

Build with `SCR_BUILD_ORACLE=ON` and the pinned `MUSASHI_DIR`, then run:

```powershell
cmake --build build/windows-oracle --config Release --target native_driving_particles_tests --parallel 4
& .\build\windows-oracle\Release\native_driving_particles_tests.exe .\analysis\assets\stunt-car-racer.scr-assets
```

Current results require matching source, asset and executable identities; use
the generated receipts rather than this guide as evidence of a passing build.

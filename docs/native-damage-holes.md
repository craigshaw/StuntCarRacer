# Damage strip at race entry

`initializeNativeDamageHoles` restores the ten chassis sections at race entry,
after the local player's saved damage has been loaded and before gameplay
warm-up. This is the original `setupDamageBar`/`renderAllDamage` path, which
lies immediately after the verified player-configuration range.
The disk loads a multiplayer driver's allowance in Practice as well as racing;
race entry retains that condition before painting the sections.

The original walks positions 9 down to 0. Positions at or above `holePosition`
use the existing-hole masked objects 23/24; lower positions use intact objects
27/28. Parity selects the pre-aligned variant. The helper draws their original
mask and four bitplanes into both planar buffers and preserves the active
drawing pointer, RNG and gameplay scalars.

These pixels are also simulation input: the damage feedback routine reads
occupancy in physical `x32..287, y0..7`. Restoring only the visible RGB image
would leave future damage progression using the wrong occupancy. The native
helper therefore initializes the original planar strip once at race entry;
ordinary presentation queries remain read-only.

The rightmost apparent position 10 is not an additional missing runtime hole.
The original starts at 10, decrements before a new hole, and first draws
position 9 at physical `x256..287`. Existing native damage events already
update every reachable hole through the damage strip. No extra renderer-owned
rightmost `x288..319` canvas is required.

## Evidence and checks

Source: the supported 408540-byte disk payload, mapped at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
Independent Capstone MC68000 decoding agrees with the REA/Ghidra observations:

| Range | Behaviour | REA evidence |
| --- | --- | --- |
| `0x5d87e..0x5d8a1` | Load per-player multiplayer damage, then initialize the strip | `ev_d44b051d062a734a16c6a45c65077bfff26dc09af78f0d2785788d4be669cbbf` |
| `0x60988..0x609ad` | Draw ten intact or previously damaged sections | `ev_ed553ecf84dca486efe727db49ef18ee62bc5e298b6b9d90fba1a0adb2368eed` |
| `0x6092e..0x60987` | Choose placement/parity and draw both buffers | `ev_a3d0b57e46689cd52ac4511569287cefbb5b3560bca39547859a951c9d6aa795` |
| `0x69f42..0x69fe7` | Original masked-object primitive | Executed by the pixel oracle |

`tests/oracle/native_damage_holes_tests.cpp` verifies the original code bytes against
the pack payload, executes the unmodified original routines in test-only
Musashi, and compares whole memory with the native helper. Coverage includes
all eleven hole states, the initialized background and varied underlying
planar pixels, repeated initialization, both buffers, object-coordinate writes,
preserved drawing-pointer/RNG state, and invalid domains. Fresh native race and
career/multiplayer integration belong to the enclosing startup/session suites.

```powershell
cmake --build build/windows-oracle --config Release --target native_damage_holes_tests --parallel 4
& .\build\windows-oracle\Release\native_damage_holes_tests.exe .\analysis\assets\stunt-car-racer.scr-assets
```

Use source, fixture and executable identities in the generated receipts to
establish current passing results. This guide describes the recovered contract.

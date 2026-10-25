# Original wheel animation

The game uses two wheel layers. The masked software wheel images are drawn
with the road at each game update. Hardware sprites supply independently
animated wheel detail at every PAL vertical blank, including the five
intervening display updates before the next 120 ms physics update. Native
rendering retains these separate clocks; smooth camera interpolation does not
change their rate.

With optional 60 FPS smoothing, all twelve captured hardware-image placements
follow the software suspension's displacement between completed frames. Using
that displacement avoids stale headers in the inactive normal/raised group. Source
rows, normal/raised image selection and the 50 Hz rotation timeline remain
discrete; no wheel sprite is blended or rescaled. Smoothing off and exact frame
queries retain their original placements. Restart/terminal boundaries cut the
interval rather than interpolating through a repositioned car.

`src/wheel_animation.cpp` recovers the original wheel-only VBlank prefix and
its helpers. `app/native_wheel_sprites.cpp` decodes and composites the hardware
images from the initialized local asset pack. Neither component executes
68000 instructions at runtime or contains graphics copied from the game.

## Recovered behavior

The original VBlank prefix at `0x616dc` decrements the two byte-sized wait
counters when nonzero. It updates wheels only while the display-update byte
at `0x5d724` is negative and the disable byte at `0x1bbb9` is zero. Pausing
uses that disable gate; it retains the animation phase and fractional position.

`updateWheelGraphics` at `0x651b8` adds the **high byte** of the actual
wheel-speed word at `0x1bc62` to the byte accumulator at `0x1bbe3`. A carry
advances the three-state phase at `0x1bbdb` once. The forward sequence is
0, 1, 2, 0; a negative local forward velocity reverses it. Reversing does not
reset the accumulator. A zero speed leaves the phase unchanged. Airborne
spin-down comes from the already recovered physics wheel-speed calculation.

The right sprite is graphics ID `37 + phase`; the left sprite is
`42 - phase`. Each independently adds six when its signed wheel height is
below 126. Both copper pointer halves are written in their original order:
low word then high word for sprite 0, followed by sprite 1. The software wheel
images retain the phase used when their game frame was drawn.

The twelve hardware images are 16 pixels wide and use two planes. Pixel zero
is transparent; pixels 1..3 use the original sprite palette, which already
stores four-bit OCS values. This differs from the three-bit source palette
used by the game's bitmap images. Sprite 0 takes priority over sprite 1.
Source rows and palette values remain in the ignored local asset pack.

The original position update at `0x69f04` changes only the two header bytes
containing vertical start and stop, retaining their previous byte-sized
difference. The native capture applies pending position updates logically,
without writing the verified simulation RAM. It translates original
horizontal coordinates by the loader's `0x80` origin and vertical coordinates
by the original sprite-Y offset. The DMA stop position is exclusive: the
original 16-row source has a 15-row visible span.

## Evidence and checks

Primary input is the 408,540-byte plaintext payload loaded at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
The enhanced reference disassembly supplied provisional names only. In
particular, its wheel-speed address and airborne decay differ from this disk.
Capstone's MC68000 decoding and Musashi running the untouched payload establish
the addresses and behavior used here.

| Original range | Recovery | REA evidence |
| --- | --- | --- |
| `0x616dc..0x6171a` | PAL VBlank prefix and gates | `ev_b9bf880030adb35bb0bdf57aed41a481cdb098da1c4af1ab81670cd9fddc10bf` |
| `0x651b8..0x6521c` | Accumulator and wheel-sprite selection | `ev_4f258cb7808058e1f8698d32dcb85d3ff1e617ea6b2d7cd8b03d30bd4a901193` |
| `0x5e744..0x5e778` | Forward/reverse phase order | `ev_e7e7d90d517c7e79773b4aa55ae2285dfd5e7aa8e984a29742ca5c6109a7156f` |
| `0x69ee0..0x69f04` | Table lookup and copper pointers | `ev_64bbd28cf477b2ae076744bfa4fa127e265c975d47bb7683ba2bc8a2bd3c8976` |
| `0x69f04..0x69f40` | Hardware sprite vertical positioning | MC68000 decoding and original execution |

The VBlank oracle stops at the audio boundary, redirecting the CPU's next
fetch to an external NOP sentinel. It does not patch or replace an original
instruction. The audio tail has its own existing oracle.

`wheel_animation_oracle` compares the complete 512 KiB memory image after
every original call, checks all sprite IDs, and checks the ordered copper
pointer writes. With one fresh native Practice seed it verifies:

- 553,987 original calls and 290,448,736,256 compared RAM bytes;
- every speed/accumulator pair, all three phases, both directions;
- every display/disable byte pair and every signed wheel-height value;
- 24,576 evolving VBlanks, preserving state through freezes and reversals;
- 4,097 original sprite-position updates;
- 9,360 compositor cases and 599,040,000 output pixels against independently
  decoded original sprite bytes, including transparency, overlap, priority and
  clipping.

All comparisons passed with zero mismatches. The pixel check models the
documented two-plane sprite display; it is not a cycle-exact custom-chip
emulation or a comparison against captured WinUAE display pixels.

Run the CMake oracle target with the supplied payload and optional named
512 KiB native race seeds:

```powershell
wheel_animation_oracle.exe analysis/reference/vesuri/offline-extracted-payload.bin practice analysis/native-game/integration/route-0/native-initial.bin
```

Reports and generated builds belong under `analysis/wheel-animation/`. The
source ADF and ROM remain unchanged.

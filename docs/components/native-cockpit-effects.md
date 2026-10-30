# Cockpit effects and indicator audit

Boost flames use the original three paired masked images: IDs 6/8, 7/9 and
49/50. The disk's frame renderer at `0x6515c..0x651b0` increments the phase
before drawing when `boostActiveFlag` is nonzero, wrapping 2 to 0. Releasing
boost hides the flames and retains their phase; the next press resumes it.
This happens once per gameplay frame, including restart warm-ups, rather than
at the host presentation rate or the wheel interrupt rate.

`RaceScene` owns the phase separately from gameplay RAM and captures it in
the completed cockpit frame. Exact, interpolated and historical views only
draw that snapshot. The original input logic still decides whether boost is
active and consumes fuel. Physics, RNG and all three clock rates are unchanged.

`native_boost_flames_tests` executes the unmodified original branch and masked
painter against native advancement and composition. It covers every boost
flag byte, all three starting phases, both target buffers, consecutive
press/release sequences and repeated drawing. The combined
`scripts/check_native_cockpit.py` also compares phase and admission after every
original main-loop frame, including the last warm-up after a restart. A fresh
drive exports all three images and checks that view queries preserve them.

## Bounded local race audit

The audit follows the supplied disk's local frame renderer `0x64e4c`, normal
game loop and race-entry drawing. It found the absent flame advancement,
[lifting chains](native-cockpit-chains.md), and
[initial damage sections](native-damage-holes.md). These now have native paths.

| Original visual path | Native owner |
| --- | --- |
| Ground dust and edge sparks | `native_driving_particles` |
| Hanging/lifting chains, objects 19–22 | `native_cockpit_chains` |
| Tops, engine/exhausts and software wheel images | `race_render_assets` |
| Boost flames | `advanceBoostFlames` / `composeBoostFlames` |
| Damage-strip sections and runtime holes | Race initialization / `driving_effects` |
| Hardware wheel sprites and 50 Hz rotation | `native_wheel_sprites` / `wheel_animation` |
| Ordinary race prompts; pause/end panel | `native_race_messages` / `renderLiveMessage` |
| Speed, fuel, lap/results, leader and distance | `NativeCockpitPresentation` |
| Road, horizon, mountains and opponent insertion | Shared scene-command pipeline |

No further omitted local indicator/effect call was found in that path. This
does not establish universal world-pixel equality, cycle-exact sprite/blitter
timing, or coverage of the deliberately deferred Computer Link mode.

## Evidence

Source: 408,540-byte payload mapped at `0xe700`, SHA-256
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
REA/Ghidra original frame assembly is retained as
`ev_e821ec28d2e99b52561d425b91f2789dbe5f41d170fec1c888eb183111e3595a`
in `analysis/rea/cockpit-effects-evidence.json`. Capstone MC68000 independently
checks the byte range; executable comparisons use Musashi only in test targets.
Current source, fixture and executable identities live in the generated
native-cockpit receipt, alongside focused comparison counts and full-loop cases.

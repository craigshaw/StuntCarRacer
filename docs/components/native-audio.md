# Native engine audio and effects

The original sound routines now have native C++ counterparts. The game still
advances verified physics once every six PAL VBlanks, or 120 ms. Audio has its own
50 Hz VBlank updates and sample-completion events. These audio updates operate
on copied inputs and separate audio state; they never write gameplay RAM or
change the retained synchronous gameplay trace.

## Recovered original behavior

| Original runtime range (end exclusive) | Native implementation | Verified behavior |
| --- | --- | --- |
| `0xf402..0xf472` | `generateEngineSamples` | Seven decimation passes, alignment and eight pointer/word-length records |
| `0x6171a..0x617c4` | `updateEngineAudio` | Audio tail of VBlank: DMA command, pitch carry, exit handling, sample-level selection and saved period |
| `0xf2cc..0xf350` | `completeAudioChannel` | Engine period installation and effects startup/completion interrupts |
| `0xf362..0xf3ee` | Existing `requestSample` | Ten ordered Paula writes for each requested effect |

The VBlank engine routine adds the current pitch delta to its 16-bit accumulator,
computes a period from `0x493e00 / (accumulator +0x17a)`, applies the original
clamps/noise bits, and halves the period while selecting a shorter sample level.
It writes the next loop's sample pointer and length, then saves the period.
Channel 0's audio interrupt installs that period. Thus pitch changes happen at
the original audio boundaries while the presentation remains smooth at 60 FPS.

Effects on channels 1..3 count the initial DMA interrupt and the following block
completion; the second event disables the channel and its interrupt. The port
also preserves the original DMA-enable guard and signed counter comparisons.
The audio-only VBlank contract starts after the original countdown and wheel
graphics work; it does not replace the whole interrupt handler. The recovered
[wheel-animation prefix](wheel-animation.md) now runs on its own presentation
state alongside this audio tail, retaining the same 50 Hz rate.

The original engine period word is at `0xf472`. The pinned enhanced source's
`audioChannel0Period` equate points elsewhere, so its symbol location cannot be
used directly for this disk. Capstone MC68000 decoding and original-byte
execution independently confirm the address.

## Asset provenance

The input payload remains the verified 408,540-byte slice at ADF offset `0xdc00`,
mapped at runtime `0xe700`. Its SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
The engine source occupies 3,172 bytes at runtime `0x19de6`, payload offset
`0xb6e6`, ADF offset `0x192e6`.

- Disk sample SHA-256: `12958d47b5bff8333c6d2e227e209890ced74a1929707b94afcb93ff6491016f`.
- Original initialization XORs each sample byte with `0x80`, converting unsigned
  disk PCM to signed playback PCM. The initialized engine SHA-256 is
  `a92d37afa3c48775577ad2e9848c1fb8bce2840f43eb2225e6c99855e43dfad2`.
- The generated bank starts at `0x1aa4a`, occupies 3,142 bytes and has SHA-256
  `ac2fc0cfed49c08351192ed1d30cd5bcf5b979f18bcca19febf59501a124f590`.

The generated word lengths are 1,586; 793; 396; 198; 99; 49; 24; and 12. Initialization
creates all eight records; the VBlank pitch routine selects indices 0..6.
The original race capture matches the initialized source bytes, the whole
generated bank and every metadata record. No sample arrays or derived PCM
assets are checked into source control.

## Verification and reproduction

`tools/research/reference/verify_engine_audio.py` verifies ADF/payload/capture hashes,
compares 97 instructions and all source operands with the pinned Vesuri source,
and independently reproduces sample generation. It uses no operand wildcards.
Its generated report is `analysis/audio-engine/reference-comparison.json`.

`tests/oracle/engine_audio_oracle.cpp` runs only the original instruction slices in
test-only Musashi and compares typed C++ state plus every ordered 16-bit Paula
write. The passing three-capture run contains 618,645 cases:

- 28 complete sample-generation cases, including captured and synthetic PCM;
- 280,925 VBlank cases, covering every accumulator and delta value at selected
  boundaries, noise bits, exits, all selected levels and continuous carry;
- 337,692 audio-interrupt cases, covering every saved period and completion
  count, DMA readbacks, guards and arbitrary channel masks.

The run executes 13,717,536 original instructions with zero mismatches. Evidence
is in `analysis/audio-engine/oracle.json`. Reproduce from PowerShell:

```powershell
. .\scripts\env.ps1
& $AmigaPython tools/research/reference/verify_engine_audio.py
cmake --preset windows-oracle
cmake --build build/windows-oracle --config Release --target engine_audio_oracle
$audioArgs = @('analysis/reference/vesuri/offline-extracted-payload.bin')
foreach ($capture in @('race', 'menu', 'menu-user')) {
    $directory = "analysis/captures/$capture"
    $manifest = Get-Content "$directory/manifest.json" -Raw | ConvertFrom-Json
    $bank = $manifest.banks | Where-Object tag -eq 'CRAM'
    $audioArgs += $capture
    $audioArgs += Join-Path $directory $bank.file
}
& build/windows-oracle/Release/engine_audio_oracle.exe @audioArgs
```

REA/Ghidra evidence is retained in `analysis/rea/audio-playback-evidence.json`:
generation `ev_83a1e3ea9fd66e5d6a1295bc2dd4ae43a771f7995df77cf35e18ca4b4bd240af`,
VBlank `ev_6d7d47ecc98c1e578109b4c34a39457f15a456fb1e2fcffccc4e275f85429c88`,
and shared effect IRQ code
`ev_ff7e7fb40fcd8c31839bdf05e75b0e8a9bd69b80cf8d85928172d6f3497b0818`.
Ghidra's shared function boundaries do not include every channel entry; the
MC68000 decoder and oracle cover the complete ranges listed above.

These proofs establish sample bytes, register commands and software state.
They do not claim an original-machine PCM recording match, cycle-exact
interrupt timing, or the Amiga's analog filter response.

## Native playback

`Drive Native.cmd` opens the original native menus; audio begins when a race
starts. **M** toggles mute. The direct-race development viewer instead starts
paused; click **Drive** or press **P** there to begin. Pausing, losing focus,
disconnecting the selected controller or reaching a terminal race state flushes
playback. Starting a race initializes audio from that race's native state;
resetting the development viewer reuses its corresponding initial state.
An automatic track recovery preserves the advancing engine pitch accumulator,
as the original restart does. Audio device failure leaves driving available;
the development viewer reports an **Audio unavailable** status.

The four-channel mixer reads signed 8-bit samples from a private copy of local
Chip RAM and regenerates the original engine bank. Channels 0+3 play left and
1+2 play right. DMA location/length latch at block boundaries; callbacks run the
recovered completion handlers. Integer integration resamples the PAL
3,546,895 Hz period clock to 48 kHz, 16-bit stereo. Windows output uses four
10 ms buffers and an event-driven worker, independently of the graphics loop.
Application gain is one half; device/system volume is never changed.

The mixer models the sample DMA behavior needed by the game. It omits bus
contention, audio attach/manual modes, Paula volume PWM and analog filtering.
The hardware clock and startup interrupt ordering follow the
[Commodore Hardware Reference Manual](https://www.ikod.se/wp-content/uploads/2020/08/Amiga_Hardware_Reference_Manual_3rd_Edition.pdf).
The output backend follows Microsoft's
[event callback](https://learn.microsoft.com/en-us/windows/win32/multimedia/using-an-callback-to-process-driver-messages)
and [audio buffer](https://learn.microsoft.com/en-us/windows/win32/multimedia/audio-data-blocks)
contracts.

Gameplay emits the same ordered effect requests as before. Each completed
physics update copies speed, wheel movement and input bits into an audio
packet. The audio side recalculates the original pitch delta using its own
advancing accumulator, then holds that delta through its 50 Hz updates. This
avoids feeding the deliberately interrupt-free verification accumulator back
into audible pitch. No audio callback can modify simulation RAM or its RNG.
The synchronous restart's internal waits remain collapsed in the native game
loop; exact original fade/wait timing is outside this audio integration.

The authored replay window remains silent; the same packets can be rendered
offline using `--audio-wav <path>` on the viewer. Standard verification now
exports the complete race to `analysis/native-render/replay-audio.wav` and
checks its stereo data, duration, headroom and 50 Hz cadence. Its 1,463 updates
produce 8,426,880 stereo frames, 8,778 VBlanks and 100 effect starts. All original
gameplay columns and the existing live/interpolation regressions still pass.

There are 16 synthetic mixer regressions and eight integration checks, including
isolated playback of all seven effects, buffer partition independence, pitch
changes, restart continuity and input immutability. Real-device checks also
cover buffer completion, pause, mute and reset:

```powershell
. .\scripts\env.ps1
& $AmigaPython scripts/check_native_rendering.py
& build/windows/Release/native_audio_tests.exe `
  analysis/captures/race/chip-000000.bin --device
```

Generated PCM and captured samples stay in ignored `analysis/` directories.

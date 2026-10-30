# Native graphics and sample startup

`app/assets/native_asset_initialization.cpp` takes zeroed 512 KiB memory containing only
the original ADF payload at `0xe700`. It accepts no RAM capture. Call it once on
fresh memory, before race-specific initialization; converting the stored audio
samples with XOR is deliberately not an idempotent operation.

It reproduces the startup memory results needed by the existing native renderer
and audio output:

- Framebuffer pointers, viewport address, sprite offset and initial sprite and
  palette copper data from original `initialize` at `0xed56`.
- Signed PCM conversion for all 46,482 source bytes at `0xf4b8..0x1aa4a`, then
  the previously verified `generateEngineSamples` native routine, including its
  eight level records and initial engine sample descriptor.
- All 52 graphics pointers and generated objects. Original bitmap objects use
  palette index 1 as transparency. Hardware sprite objects occupy the original
  contiguous allocation, unlike the enhanced reference source's split buffers.
- The font patch at `0x5aa02..0x5aa38`, including its full longword clears.
- Both planar cockpit background buffers, decoded from the original compressed
  bytes. The temporary graphics sheet is overwritten only after extracting the
  software objects which need it.

The payload source is the supplied ADF at offset `0xdc00`, length 408,540,
loaded at `0xe700`. Its SHA-256 is
`508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c`.
No source image, sample, extracted array or captured state is embedded in these
tracked files. The local asset pack supplies the payload bytes.

## Verification

Configure the oracle build with the existing local Musashi checkout and build
`native_asset_initialization_tests`. Then run:

```powershell
. .\scripts\env.ps1
& $AmigaPython tools/research/reference/verify_native_asset_initialization.py `
  --oracle build/windows-oracle/Release/native_asset_initialization_tests.exe `
  --capture analysis/captures/race
```

The capture argument is optional and used only for independent comparisons.
The test always creates the native memory from zero plus ADF bytes. It separately
executes four unmodified original entry points using test-only MC68000 Musashi:
`initialize`, `initializeGraphicsData`, the font patch and
`copyMainGameBackground`. Every byte of native and original memory agrees except
the 28 bytes of original hardware interrupt vectors at `0x64..0x80`, which a
native Windows program must not install. Original custom-register and CIA writes
are accepted by the test peripheral stub; the native initializer omits them.

With the independent Practice capture comparison enabled, all 52 object
pointers, immutable object contents and the patched font also agree. Hardware
sprite position headers are mutable and are excluded from that comparison.
The current result is 557,244 byte comparisons, zero mismatches, with 1,432,585
original 68000 instructions executed.

The wrapper saves hashes, source offsets, source-file identities and results in
ignored `analysis/native-assets-init/check-results.json`. REA/Ghidra evidence is
retained locally in `analysis/rea/native-startup-evidence.json`; original operands
were independently checked with Capstone MC68000 mode. Relevant REA evidence IDs:

- `initialize`: `ev_ee09524338ca4cb4b1cfcf73a4f3b153bd547a532a56e69de64a237e6a2cb9e0`
- Graphics initialization: `ev_5c768550df78b85825678438cf671a008f1e7764e5dbaa97eddec8733fc36f6a`
- Graphics element loading: `ev_3831d11eac7433c2ec3d62e02122ef3c0ff2b5b343f3b0b40385c1101b227664`

These comparisons establish startup memory results. They do not establish
Amiga DMA timing, display-beam timing or an emulated operating system.

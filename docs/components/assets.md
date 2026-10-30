# Assets and initialization

Import and decoding live in [app/assets/](../../app/assets/). The player starts
from fresh native state; the pack's retained original code bytes are used only
as data. Original execution belongs to separate verification programs.

## Import

The supported ADF is 901,120 bytes, SHA-256:
`548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
The [research guide](../research/reverse-engineering.md#verified-disk-and-memory-mapping)
records its payload mapping and reference provenance.

Native import and `scripts/import_native_assets.py` enforce the same identity
and extraction recipe. Import writes a `.scr-assets` pack and JSON provenance;
the source ADF stays read-only. Loading rejects changed content, wrong extents
and unsupported versions. A failed replacement preserves the last valid pack.
Player paths and development overrides are documented in
[playing](../playing.md#data-and-recovery) and [development](../development.md).

## Initialization order

1. Map the validated payload into zeroed native memory and call
   `initializeNativeAssets` once. Sample conversion is not idempotent.
2. Apply `initializeNativeLoadState`, then `initializeNativeGameData`. The load
   transition supplies validation state used by steering; the game-data reset
   clears mutable track tables.
3. Initialize the selected track. For Draw Bridge, update raw geometry before
   rebuilding its cache.
4. Initialize the race, including damage occupancy, text mode and the two
   recovered warm-up frames. A captured Practice state is never a startup input.

Use these entry points when adding a launch path; manually assembling their
outputs can leave an apparently playable state with disabled or incorrect rules.

## Source-control safeguards

Keep original media, extracted assets, packs, captures and generated previews
outside Git, including data disguised as source arrays or encoded text. Preserve
source hashes and offsets with derived files under ignored `analysis/`.

The indexed-blob asset checker and release resource/file-list checker are described
in [development](../development.md). The approved launcher
logo is the explicit exception recorded in [third-party notices](../../THIRD_PARTY_NOTICES.md).

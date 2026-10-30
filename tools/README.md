# Development tools

`replay/native_replay_viewer.cpp` is the Windows replay/direct-race viewer;
`replay/driving_replay_native.cpp` runs a headless native replay. Both reuse the
production implementations. `SCR_BUILD_DEV_TOOLS` controls these targets without
enabling original-code tests. Only the player application and licence notices are installed to `dist/`.

The captured replay shortcut is in `replay/`; the original-game emulator shortcut
is in `research/`.

Everyday launch and verification commands remain in `scripts/`; optional
recovery and emulator utilities live in [research/](research/README.md). See the
[development guide](../docs/development.md) and the separate
[viewer controls](../docs/components/native-replay-viewer.md).

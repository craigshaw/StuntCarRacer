# Development tools

`replay/native_replay_viewer.cpp` is the Windows replay/direct-race viewer;
`replay/driving_replay_native.cpp` runs a headless native replay. Both reuse the
production implementations. `SCR_BUILD_DEV_TOOLS` controls these targets without
enabling original-code tests. Only the player application is installed to `dist/`.

Launch and verification commands remain in `scripts/`. See the
[development guide](../docs/development.md) and the separate
[viewer controls](../docs/native-replay-viewer.md).

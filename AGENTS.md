# Stunt Car Racer native port

The initial local native port is the faithful baseline for future enhancements.
Start with README.md, docs/development.md and docs/architecture.md. Use
docs/README.md to find component contracts and evidence without reading every
guide. Inspect Git status and preserve existing work before editing.

- The Windows game uses ported C++17, native assets, input, audio and a software
  renderer. No emulated CPU, ROM or captured Practice state is needed to play.
  Computer Link is deliberately deferred; do not implement it without a request.
- Preserve original fixed-point arithmetic, update order, RNG and race rules.
  Gameplay stays at 120 ms per update; wheel/engine interrupt state at 50 Hz;
  host presentation targets 60 Hz. Interpolation and alternate views must never
  mutate simulation state. Keep future visual options separate from the faithful
  baseline. Keep both 4:3 views and the optional 16:9/32:9 world/frame presentation
  independent of simulation; the original car artwork retains its scale.
- Use CMake presets and CTest; native Release/Debug share build/windows/ and
  original-code tests use build/windows-oracle/. Stage the player with CMake
  Install to dist/Stunt Car Racer.exe. Keep research evidence under analysis/.
  Drive Native.cmd remains a provisional build/check/launch helper. Direct-race
  and captured replay viewers are development tools with different controls.
- Shared runtime adapters belong in src/state/, development entry points in
  tools/replay/, and tests in unit/, integration/, oracle/ and support/ under
  tests/. Production must not include test headers or link oracle code.
- Choose checks using docs/development.md. Original-code comparisons belong
  only to test executables (SCR_BUILD_ORACLE); never link Musashi into the game.
  Retained passes require matching source, fixture and executable identities.
- Keep entry documentation current and concise. Preserve useful recovered
  contracts and evidence boundaries in component guides, not a session diary.
- Every commit includes: Co-authored-by: Codex <codex@openai.com>

## Assets and reverse engineering

- Preserve the supplied ADF and ROM. Write derived files under analysis/ and
  emulator state/working copies under working/. Record source hashes and offsets.
- REA 6.7.0 is installed globally and registered as `rea` in the Windows user's
  shared Codex configuration. Its matching user skill is
  `C:/Users/craig/.agents/skills/reverse-engineer-anything/SKILL.md`.
- Use the shared `rea_wsl` MCP server for raw m68k analysis. It runs REA 6.7.0
  and Ghidra 12.1.4 in Ubuntu-24.04 as craig, and passed real 68000 CLI/MCP tests
  with verified file hashes and load mapping. Pass Linux paths such as
  `/mnt/c/Projects/Stunt Car Racer/analysis/...`. The `rea-wsl` PowerShell helper
  in scripts/env.ps1 provides the same CLI. ADF/Hunk containers still need
  extraction and justified load addresses before raw import.
- The separate native Windows `rea` server still does **not** admit m68k.
  Do not disguise m68k as another CPU or file format. Native Ghidra and Capstone
  remain useful independent cross-checks.
- Dot-source scripts/env.ps1 for local tool paths. Machine-specific overrides
  belong in ignored setup/local-tools.json; setup/README.md describes discovery.
  Everyday helpers live in scripts/, optional research utilities in tools/research/.
  Keep component contracts in docs/components/ and research guides in docs/research/.
- Ghidra `68000:BE:32:default` decodes a 68040 superset. Cross-check questionable
  instructions against Capstone's MC68000 mode or WinUAE's 68000 execution.
- The ADF has a valid boot checksum but no valid ordinary AmigaDOS root block.
  The recovered plaintext payload is disk offset 0xdc00, length 408540, loaded
  at 0xe700. A WinUAE menu snapshot corroborates the mapping. Start with
  analysis/map/report.txt and manifest.json; docs/research/reverse-engineering.md documents
  reproduction.
  Do not assume Hunk files or repeat extraction discovery. The pinned Vesuri
  disassembly is enhanced: its timestep and scheduling differ from this disk.
  Imported labels remain provisional except where comparison evidence says otherwise.
- Boot-block listing addresses currently mean disk byte offsets, not confirmed
  runtime addresses. Distinguish observed facts, hypotheses, and synthetic tests.
- Keep copyrighted inputs, RAM dumps, generated binaries, and Ghidra databases
  out of source control. .gitignore already excludes their local output folders.

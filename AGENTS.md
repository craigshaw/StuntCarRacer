# Stunt Car Racer reverse engineering

This project investigates the original Amiga game. Start with README.md for the
verified toolchain, limitations, and launch commands.

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
- Dot-source scripts/env.ps1 for the local tool paths. The isolated Python is
  `C:/Tools/amiga-re/venv/Scripts/python.exe`.
- Ghidra `68000:BE:32:default` decodes a 68040 superset. Cross-check questionable
  instructions against Capstone's MC68000 mode or WinUAE's 68000 execution.
- The ADF has a valid boot checksum but no valid ordinary AmigaDOS root block.
  The recovered plaintext payload is disk offset 0xdc00, length 408540, loaded
  at 0xe700. A WinUAE menu snapshot corroborates the mapping. Start with
  analysis/map/report.txt and manifest.json; README.md documents reproduction.
  Do not assume Hunk files or repeat extraction discovery. The pinned Vesuri
  disassembly is enhanced: its timestep and scheduling differ from this disk.
  Imported labels remain provisional except where comparison evidence says otherwise.
- Boot-block listing addresses currently mean disk byte offsets, not confirmed
  runtime addresses. Distinguish observed facts, hypotheses, and synthetic tests.
- Keep copyrighted inputs, RAM dumps, generated binaries, and Ghidra databases
  out of source control. .gitignore already excludes their local output folders.

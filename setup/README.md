# Local tools

The native player needs CMake and Visual Studio C++ Build Tools to build. Python,
WinUAE, Java, Ghidra and REA are optional development/research tools.

Copy `local-tools.example.json` to `local-tools.json` and set only the overrides
you need. The local file is ignored; preserve an existing copy when updating source.

| Key | Meaning / discovery when omitted |
| --- | --- |
| `python` | Python executable; `SCR_PYTHON`, checkout `.venv`, then `python`/`python3` on PATH |
| `winUae` | WinUAE executable; `SCR_WINUAE`, then `winuae64`/`winuae` on PATH |
| `ghidra` | Ghidra installation folder; existing `GHIDRA_INSTALL_DIR`, then `analyzeHeadless` on PATH |
| `javaHome` | JDK folder; existing `JAVA_HOME`, then `javac` on PATH |
| `reaPackage` | Installed `rea-agents` package folder; `SCR_REA_PACKAGE`, then the standard global npm location |
| `wslDistribution`, `wslUser` | Optional WSL selection; omitted values use WSL defaults |
| `wslRea` | Linux bridge executable; omitted uses `$HOME/.local/bin/rea-wsl` in WSL |

Use absolute paths with forward slashes in JSON. Local settings take precedence
over discovery. In PowerShell:

```powershell
. .\scripts\env.ps1
& (Get-ScrPython) --version
```

Dot-sourcing does not launch tools or install anything. Missing optional tools
are reported when their launcher/check needs them. Native CMake commands do not
source this file. Generator overrides belong in ignored `CMakeUserPresets.json`.

`requirements.txt` pins Python research dependencies; `musashi-source-lock.json`
pins the test-only CPU source. `install-rea-wsl.sh` provisions the optional Linux
tools; `register-rea-wsl.py` uses the local WSL choices when explicitly run to
register that bridge. Neither runs during ordinary builds or checks.

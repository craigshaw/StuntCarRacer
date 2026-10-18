# Dot-source this file before using the native reverse-engineering tools.
$env:GHIDRA_INSTALL_DIR = 'C:\Tools\ghidra_12.1.4_PUBLIC'
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-23'
$env:Path = "$env:JAVA_HOME\bin;C:\Tools\amiga-re\venv\Scripts;" + $env:Path
$AmigaPython = 'C:\Tools\amiga-re\venv\Scripts\python.exe'
$WinUae = 'C:\Tools\WinUAE-6.0.3\winuae64.exe'

# Pass Linux paths (/mnt/c/...) for target files when using this bridge.
function rea-wsl {
    & "$env:SystemRoot\System32\wsl.exe" --distribution Ubuntu-24.04 --user craig --exec /home/craig/.local/bin/rea-wsl @args
}

# Dot-source for optional local tools. Native CMake builds need none of these.
$ScrProjectRoot = Split-Path -Parent $PSScriptRoot
$ScrToolsFile = Join-Path $ScrProjectRoot 'setup/local-tools.json'
$ScrTools = if (Test-Path -LiteralPath $ScrToolsFile) {
    Get-Content -LiteralPath $ScrToolsFile -Raw | ConvertFrom-Json
} else { [pscustomobject]@{} }

function Find-ScrTool([string]$Configured, [string]$Environment, [string[]]$Commands) {
    if ($Configured) { return $Configured }
    if ($Environment) { return $Environment }
    foreach ($command in $Commands) {
        $found = Get-Command $command -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.Source }
    }
    return $null
}

$localPython = @('.venv/Scripts/python.exe', '.venv/bin/python') |
    ForEach-Object { Join-Path $ScrProjectRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$pythonChoice = if ($ScrTools.python) { $ScrTools.python } elseif ($env:SCR_PYTHON) { $env:SCR_PYTHON } else { $localPython }
$AmigaPython = Find-ScrTool $pythonChoice $null @('python', 'python3')
$WinUae = Find-ScrTool $ScrTools.winUae $env:SCR_WINUAE @('winuae64', 'winuae')

if ($ScrTools.ghidra) { $env:GHIDRA_INSTALL_DIR = $ScrTools.ghidra }
if (-not $env:GHIDRA_INSTALL_DIR) {
    $headless = Find-ScrTool $null $null @('analyzeHeadless.bat', 'analyzeHeadless')
    if ($headless) { $env:GHIDRA_INSTALL_DIR = Split-Path (Split-Path $headless -Parent) -Parent }
}
if ($ScrTools.javaHome) { $env:JAVA_HOME = $ScrTools.javaHome }
if (-not $env:JAVA_HOME) {
    $javaCompiler = Find-ScrTool $null $null @('javac')
    if ($javaCompiler) { $env:JAVA_HOME = Split-Path (Split-Path $javaCompiler -Parent) -Parent }
}
foreach ($toolDirectory in @($(if ($env:JAVA_HOME) { Join-Path $env:JAVA_HOME 'bin' }),
                            $(if ($AmigaPython) { Split-Path $AmigaPython -Parent }))) {
    if ($toolDirectory -and (Test-Path -LiteralPath $toolDirectory) -and
        $toolDirectory -notin ($env:Path -split [IO.Path]::PathSeparator)) {
        $env:Path = $toolDirectory + [IO.Path]::PathSeparator + $env:Path
    }
}

function Get-ScrPython {
    if (-not $AmigaPython) { throw 'Python not found. Set python in setup/local-tools.json or SCR_PYTHON, or add Python to PATH.' }
    return $AmigaPython
}
function Get-ScrWinUae {
    if (-not $WinUae) { throw 'WinUAE not found. Set winUae in setup/local-tools.json or SCR_WINUAE, or add WinUAE to PATH.' }
    return $WinUae
}

# Pass Linux paths (/mnt/c/...) for target files. Optional WSL choices stay local.
function rea-wsl {
    $bridgeArguments = @()
    if ($ScrTools.wslDistribution) { $bridgeArguments += @('--distribution', $ScrTools.wslDistribution) }
    if ($ScrTools.wslUser) { $bridgeArguments += @('--user', $ScrTools.wslUser) }
    if ($ScrTools.wslRea) { $bridgeArguments += @('--exec', $ScrTools.wslRea) }
    else { $bridgeArguments += @('--exec', 'sh', '-lc', 'exec "$HOME/.local/bin/rea-wsl" "$@"', 'rea-wsl') }
    & (Join-Path $env:SystemRoot 'System32/wsl.exe') @bridgeArguments @args
}

param(
    [string]$Inputs = 'tests/replays/laps/authored-race-1463.csv',
    [ValidateRange(0,1000000)][int]$StartTick = 0,
    [switch]$Paused,
    [switch]$ExactTicks,
    [switch]$Live,
    [switch]$RefreshReference,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$pythonTool = 'C:\Tools\amiga-re\venv\Scripts\python.exe'
$reportPath = Join-Path $taskRoot 'analysis/native-render/check-results.json'
function Resolve-ReplayPath([string]$Value) {
    if ([System.IO.Path]::IsPathRooted($Value)) { return $Value }
    return Join-Path $taskRoot $Value
}
$checkArguments = @((Join-Path $taskRoot 'scripts/check_native_rendering.py'), '--inputs', (Resolve-ReplayPath $Inputs), '--output', $reportPath)
if ($RefreshReference) { $checkArguments += '--refresh-reference' }
if ($SkipBuild) { $checkArguments += '--skip-build' }
Push-Location -LiteralPath $taskRoot
try {
    & $pythonTool @checkArguments
    if ($LASTEXITCODE -ne 0) { throw 'Native replay verification failed. See analysis/native-render/.' }
    $checked = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($checked.status -ne 'passed') { throw 'Native replay has no passing verification report.' }
    if ($StartTick -ge $checked.inputs.ticks) { throw 'StartTick is outside the selected replay.' }
    $executable = Resolve-ReplayPath $checked.executable.path
    $viewerArguments = @(
        '--chip', (Resolve-ReplayPath $checked.capture.chip_ram.path),
        '--payload', (Resolve-ReplayPath $checked.payload.path),
        '--inputs', (Resolve-ReplayPath $checked.inputs.identity.path),
        '--reference', (Resolve-ReplayPath $checked.original_reference.trace.path),
        '--trace', (Join-Path $taskRoot 'analysis/native-render/window-native.csv'),
        '--report', (Join-Path $taskRoot 'analysis/native-render/window-report.json'),
        '--start-tick', [string]$StartTick
    )
    if ($Paused) { $viewerArguments += '--paused' }
    if ($ExactTicks) { $viewerArguments += '--no-interpolation' }
    if ($Live) { $viewerArguments += @('--live', '--recordings-dir', (Join-Path $taskRoot 'analysis/live-driving')) }
    # ArgumentList is passed directly to this locally built Windows executable.
    # File paths cannot contain quotation marks; quotes preserve their spaces.
    $quotedArguments = ($viewerArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory $taskRoot -WindowStyle Normal | Out-Null
}
finally { Pop-Location }

param([switch]$Analysis, [switch]$Settings)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot "../../scripts/env.ps1")
$WinUae = Get-ScrWinUae
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$config = Join-Path $projectRoot 'working\stunt-car-racer.uae'
if (!(Test-Path -LiteralPath $config)) {
    & (Get-ScrPython) "$PSScriptRoot\prepare_emulator.py"
    if ($LASTEXITCODE -ne 0) { throw 'Emulator preparation failed' }
}
$playConfig = Join-Path $projectRoot 'working\stunt-car-racer-play.uae'
if (!$Analysis -and (Test-Path -LiteralPath $playConfig)) { $config = $playConfig }
$captureDir = Join-Path $projectRoot 'working\captures'
New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
$capture = Join-Path $captureDir ("play-{0}.uss" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$gui = if ($Settings) { 'yes' } else { 'no' }
# This is an interactive launcher. Normal exit also preserves a unique snapshot.
Start-Process -FilePath $WinUae -WorkingDirectory (Join-Path $projectRoot 'working') -ArgumentList @(
    '-f', ('"{0}"' -f $config), '-s', "use_gui=$gui",
    '-s', ('"statefile_quit={0}"' -f $capture)
)
Write-Host "Using $config"
Write-Host "Normal exit will save $capture"

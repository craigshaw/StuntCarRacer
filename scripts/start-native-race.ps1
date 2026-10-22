param(
    [string]$Adf,
    [string]$AssetPack = 'analysis/assets/stunt-car-racer.scr-assets',
    [ValidateRange(0,7)][int]$Track = 0,
    [switch]$ExactTicks,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'env.ps1')
$pythonTool = Get-ScrPython
if (-not [System.IO.Path]::IsPathRooted($AssetPack)) { $AssetPack = Join-Path $taskRoot $AssetPack }
$checkArguments = @((Join-Path $PSScriptRoot 'check_native_startup.py'), '--pack', $AssetPack)
if ($Adf) { $checkArguments += @('--adf', $Adf) }
if ($SkipBuild) { $checkArguments += '--skip-build' }
Push-Location -LiteralPath $taskRoot
try {
    & $pythonTool (Join-Path $PSScriptRoot 'check_repository_assets.py')
    if ($LASTEXITCODE -ne 0) { throw 'Original asset data was found in the Git index.' }
    & $pythonTool @checkArguments
    if ($LASTEXITCODE -ne 0) { throw 'Native startup verification failed. See analysis/native-startup/.' }
    $checked = Get-Content (Join-Path $taskRoot 'analysis/native-startup/check-results.json') -Raw | ConvertFrom-Json
    $executable = Join-Path $taskRoot $checked.viewer.path
    $arguments = @('--asset-pack', $AssetPack, '--track', [string]$Track,
        '--trace', (Join-Path $taskRoot 'analysis/native-startup/window-native.csv'),
        '--report', (Join-Path $taskRoot 'analysis/native-startup/window-report.json'),
        '--recordings-dir', (Join-Path $taskRoot 'analysis/live-driving'))
    if ($ExactTicks) { $arguments += '--no-interpolation' }
    $quotedArguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory $taskRoot -WindowStyle Normal | Out-Null
}
finally { Pop-Location }

param(
    [string]$Adf,
    [string]$AssetPack = 'analysis/assets/stunt-car-racer.scr-assets',
    [string]$SaveFile = 'working/saves/stunt-car-racer.scr-save',
    [string]$SettingsFile = 'working/settings/display.ini',
    [switch]$Fullscreen,
    [switch]$Interpolate,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'env.ps1')
$pythonTool = Get-ScrPython
if (-not [System.IO.Path]::IsPathRooted($AssetPack)) { $AssetPack = Join-Path $taskRoot $AssetPack }
if (-not [System.IO.Path]::IsPathRooted($SaveFile)) { $SaveFile = Join-Path $taskRoot $SaveFile }
if (-not [System.IO.Path]::IsPathRooted($SettingsFile)) { $SettingsFile = Join-Path $taskRoot $SettingsFile }
$checkArguments = @((Join-Path $PSScriptRoot 'check_native_game.py'), '--pack', $AssetPack)
if ($Adf) { $checkArguments += @('--adf', $Adf) }
if ($SkipBuild) { $checkArguments += '--skip-build' }
Push-Location -LiteralPath $taskRoot
try {
    & $pythonTool (Join-Path $PSScriptRoot 'check_repository_assets.py')
    if ($LASTEXITCODE -ne 0) { throw 'Original asset data was found in the Git index.' }
    & $pythonTool @checkArguments
    if ($LASTEXITCODE -ne 0) { throw 'Native game checks failed. See analysis/native-game/.' }
    $checked = Get-Content (Join-Path $taskRoot 'analysis/native-game/check-results.json') -Raw | ConvertFrom-Json
    $executable = Join-Path $taskRoot $checked.game.path
    $arguments = @('--asset-pack', $AssetPack, '--recordings-dir', (Join-Path $taskRoot 'analysis/live-driving'), '--save-file', $SaveFile)
    $arguments += @('--settings-file', $SettingsFile)
    if ($Fullscreen) { $arguments += '--fullscreen' }
    if ($Interpolate) { $arguments += '--interpolate' }
    $quotedArguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory $taskRoot -WindowStyle Normal | Out-Null
}
finally { Pop-Location }

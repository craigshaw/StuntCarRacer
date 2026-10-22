$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot "../../scripts/env.ps1")
if (-not $env:GHIDRA_INSTALL_DIR) { throw 'Set ghidra in setup/local-tools.json or GHIDRA_INSTALL_DIR before running this research check.' }
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$output = Join-Path $projectRoot 'analysis\toolchain'
$projects = Join-Path $projectRoot 'analysis\ghidra'
New-Item -ItemType Directory -Force -Path $output, $projects | Out-Null
$fixture = Join-Path $output 'return-five.bin'
[IO.File]::WriteAllBytes($fixture, [byte[]](0x70, 0x05, 0x4e, 0x75))
$log = Join-Path $output 'ghidra-m68k.log'
$projectName = 'ToolchainSmoke-' + [Guid]::NewGuid().ToString('N')
& "$env:GHIDRA_INSTALL_DIR\support\analyzeHeadless.bat" $projects $projectName `
    -import $fixture -processor '68000:BE:32:default' -cspec default `
    -loader BinaryLoader -loader-baseAddr 0x1000 `
    -scriptPath "$PSScriptRoot\ghidra" -postScript SmokeM68k.java `
    -noanalysis -deleteProject *> $log
if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'M68K_SMOKE_PASSED' -Quiet)) {
    Get-Content -LiteralPath $log -Tail 40
    throw "Ghidra 68000 verification failed; see $log"
}
Write-Output "Ghidra 68000 disassembly/decompilation passed. Evidence: $log"

# SPDX-License-Identifier: ISC
#
# Decompile lib/wlc_hybrid.o_shipped with Ghidra (headless) into re-out/ghidra
# and build the cross reference index. Windows version of decompile.sh.
#
#   decompile.ps1 [-Ghidra DIR] [-Jdk DIR]
#
# DIR defaults: the newest ghidra_* and jdk-* below re-out/tools.
param(
    [string]$Ghidra,
    [string]$Jdk
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $root 're-out'
$tools = Join-Path $out 'tools'
if (-not $Ghidra) {
    $Ghidra = (Get-ChildItem $tools -Directory -Filter 'ghidra_*' | Sort-Object Name | Select-Object -Last 1).FullName
}
if (-not $Jdk) {
    $Jdk = (Get-ChildItem $tools -Directory -Filter 'jdk-*' | Sort-Object Name | Select-Object -Last 1).FullName
}
if (-not $Ghidra) { throw "Ghidra not found: unpack it below $tools or pass -Ghidra" }
if ($Jdk) {
    $env:JAVA_HOME = $Jdk
    $env:PATH = "$Jdk\bin;$env:PATH"
}
$blob = Join-Path $root 'lib\wlc_hybrid.o_shipped'
if (-not (Test-Path $blob)) { throw "$blob is missing (make fetch)" }

New-Item -ItemType Directory -Force (Join-Path $out 'ghidra'), (Join-Path $out 'ghidra-proj') | Out-Null
Push-Location $PSScriptRoot
try {
    python blob.py funcs | ForEach-Object { ($_ -split '\s+')[0] } |
        Set-Content -Encoding ascii (Join-Path $out 'entries.txt')
    & (Join-Path $Ghidra 'support\analyzeHeadless.bat') (Join-Path $out 'ghidra-proj') bcm4360 `
        -import $blob -overwrite -scriptPath (Join-Path $PSScriptRoot 'ghidra') `
        -postScript ExportDecomp.java (Join-Path $out 'ghidra') (Join-Path $out 'entries.txt') 2>&1 |
        Tee-Object -FilePath (Join-Path $out 'ghidra-run.log') |
        Select-String -Pattern 'ExportDecomp.java>|ERROR|REPORT'
    python index.py build
}
finally {
    Pop-Location
}

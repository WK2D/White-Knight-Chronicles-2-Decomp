# Project-local environment only. No registry or global PATH changes.
param([string]$Workspace = (Resolve-Path "$PSScriptRoot/../../.."))
$ErrorActionPreference = 'Stop'
$toolRoot = Join-Path $Workspace 'work/tools/llvm/bin'
$vcRoot = 'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/MSVC/14.29.30133'
$sdkRoot = 'C:/Program Files (x86)/Windows Kits/10'
$sdkVersion = '10.0.19041.0'
foreach ($required in @("$toolRoot/clang-cl.exe", "$vcRoot/include", "$sdkRoot/Include/$sdkVersion/um")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build prerequisite: $required" }
}
$env:PATH = "$toolRoot;$Workspace/work/venv/Scripts;$vcRoot/bin/Hostx64/x64;$sdkRoot/bin/$sdkVersion/x64;$env:PATH"
$env:INCLUDE = "$vcRoot/include;$sdkRoot/Include/$sdkVersion/ucrt;$sdkRoot/Include/$sdkVersion/shared;$sdkRoot/Include/$sdkVersion/um;$sdkRoot/Include/$sdkVersion/winrt"
$env:LIB = "$vcRoot/lib/x64;$sdkRoot/Lib/$sdkVersion/ucrt/x64;$sdkRoot/Lib/$sdkVersion/um/x64"
$env:CC = 'clang-cl'
$env:CXX = 'clang-cl'

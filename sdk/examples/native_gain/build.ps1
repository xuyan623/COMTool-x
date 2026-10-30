param(
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$QtPrefix
)
$ErrorActionPreference = 'Stop'
$sdkRoot = (Resolve-Path -LiteralPath $Sdk).Path
$qtRoot = (Resolve-Path -LiteralPath $QtPrefix).Path
& cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$sdkRoot;$qtRoot"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build (Join-Path $PSScriptRoot 'build') --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-Item -LiteralPath (Join-Path $PSScriptRoot 'build\NativeGain.crpl')

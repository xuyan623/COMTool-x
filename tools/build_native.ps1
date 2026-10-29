param(
    [string]$QtPrefix = 'D:\Software\DevTools\Qt\6.12.0\msvc2022_64',
    [string]$Configuration = 'Release',
    [switch]$Test,
    [switch]$CoreOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vsRoot = 'D:\Software\DevTools\Microsoft Visual Studio\2022\BuildTools'
$devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$ninjaDir = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (!(Test-Path -LiteralPath $devCmd)) { throw '找不到本机 Visual Studio C++ 环境。' }
if (!(Test-Path -LiteralPath (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'))) { throw '请先准备匹配的 Qt C++ SDK。' }
# Import compiler variables into this build process only; never change the
# machine/user PATH or print the inherited environment, which may hold secrets.
$compilerEnvironment = & cmd.exe /d /c "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio 环境初始化失败。' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=][^=]*)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$env:PATH = "$QtPrefix\bin;$ninjaDir;$env:PATH"
$buildPath = Join-Path $repoRoot 'build\native'
$buildApplication = if ($CoreOnly) { 'OFF' } else { 'ON' }
& cmake -S $repoRoot -B $buildPath -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCX_BUILD_APP=$buildApplication"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $buildPath --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) {
    & ctest --test-dir $buildPath --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

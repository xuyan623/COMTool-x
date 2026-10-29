param(
    [string]$QtPrefix = 'D:\Software\DevTools\Qt\6.12.0\msvc2022_64',
    [string]$Python = '',
    [switch]$SkipVerify
)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
if ($Python) {
    if (!(Test-Path -LiteralPath $Python -PathType Leaf)) { throw 'Choose an existing local Python interpreter for extension tests' }
    $env:CORIG_TEST_PYTHON = [IO.Path]::GetFullPath($Python)
}
& (Join-Path $repoRoot 'tools\build_native.ps1') -QtPrefix $QtPrefix -Test
if ($LASTEXITCODE -ne 0) { throw 'Native build or C++ tests failed' }
& (Join-Path $repoRoot 'tools\package_native.ps1') -QtPrefix $QtPrefix
if (!$SkipVerify -and $Python) {
    & $Python (Join-Path $repoRoot 'tools\native_release_check.py') --executable (Join-Path $repoRoot 'build\native-release\CoRig\CoRig.exe') --data-dir (Join-Path $repoRoot 'build\qa-build-release') --output (Join-Path $repoRoot 'verification\corig-packaged')
    if ($LASTEXITCODE -ne 0) { throw 'Deployed desktop checks failed' }
    & (Join-Path $repoRoot 'tools\package_native.ps1') -QtPrefix $QtPrefix -ArchiveOnly
}
Write-Host 'Portable build: dist\CoRig-Windows-x64.zip'
Write-Host 'Corresponding source: dist\CoRig-source.zip'

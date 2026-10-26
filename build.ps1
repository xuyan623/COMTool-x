param([switch]$SkipVerify)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
Set-Location -LiteralPath $taskRoot

# The locked environment is reproducible; Python is needed only on the build machine.
uv sync --locked --group build --python 3.15
if ($LASTEXITCODE -ne 0) { throw 'Dependency synchronization failed' }
$taskPython = Join-Path $taskRoot '.venv\Scripts\python.exe'
$env:QT_QPA_PLATFORM = 'offscreen'
& $taskPython -m unittest discover -s tests -v
if ($LASTEXITCODE -ne 0) { throw 'Behavioural checks failed' }
& $taskPython -m tools.package
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
if (-not $SkipVerify) {
    & $taskPython -m tools.integration_check --executable (Join-Path $taskRoot 'build\release\COMTool-x\COMTool-x.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Packaged integration checks failed' }
    & $taskPython -m tools.api_discovery_check --executable (Join-Path $taskRoot 'build\release\COMTool-x\COMTool-x.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Multi-window API discovery checks failed' }
}
& $taskPython -m tools.package --archive-only
if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed' }
$publishDirectory = Join-Path $taskRoot 'dist'
New-Item -ItemType Directory -Path $publishDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\release\COMTool-x-Windows-x64.zip') -Destination (Join-Path $publishDirectory 'COMTool-x-Windows-x64.zip') -Force
Write-Host 'Portable package: dist\COMTool-x-Windows-x64.zip'

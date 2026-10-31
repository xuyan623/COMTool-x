param(
    [string]$QtPrefix = 'D:\Software\DevTools\Qt\6.12.0\msvc2022_64',
    [switch]$ArchiveOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'build\native-release'
$releaseRoot = Join-Path $outputRoot 'CoRig'
$sourceStage = Join-Path $outputRoot 'source'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (!$ArchiveOnly) {
    # Rebuild only this script's generated staging folder. Validate the exact
    # resolved target before recursive removal; never touch projects or data.
    if (Test-Path -LiteralPath $releaseRoot) {
        $resolvedTarget = (Resolve-Path -LiteralPath $releaseRoot).Path
        if ($resolvedTarget -ne [IO.Path]::GetFullPath($releaseRoot) -or !(Split-Path -Parent $resolvedTarget).Equals([IO.Path]::GetFullPath($outputRoot),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected release staging path' }
        Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
    }
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    foreach ($program in @('CoRig.exe','CoRig-cli.exe','CoRig-uninstall.exe','CoRigSdk.dll')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "build\native\$program") -Destination $releaseRoot
    }
    # Qt's dependency scanner includes the actual QML imports and the C++
    # runtime. The application and official plugins carry no Python runtime.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtPrefix 'bin\windeployqt.exe') --release --qmldir (Join-Path $repoRoot 'src\qml') --no-translations --no-opengl-sw (Join-Path $releaseRoot 'CoRig.exe') 2>&1 | Out-Null
    $deployExit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($deployExit -ne 0) { throw 'Qt deployment failed' }
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtPrefix 'bin\windeployqt.exe') --release --dir $releaseRoot --no-translations --no-opengl-sw (Join-Path $repoRoot 'build\native\corig_official.dll') 2>&1 | Out-Null
    $deployExit = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    if ($deployExit -ne 0) { throw 'Official plugin Qt deployment failed' }
    New-Item -ItemType Directory -Path (Join-Path $releaseRoot 'plugins') -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build\native\plugins') -Directory | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $releaseRoot 'plugins') -Recurse -Force }
    Set-Content -LiteralPath (Join-Path $releaseRoot 'portable.ini') -Value "[CoRig]`nportable=true" -Encoding ascii
}
if (!(Test-Path -LiteralPath (Join-Path $releaseRoot 'CoRig.exe'))) { throw 'Build and deploy before archiving' }
$references = Join-Path $repoRoot 'skills\corig\references'
foreach ($name in @('API.md','PLUGIN_SDK.md','COMMAND_SCHEMA.json')) { Copy-Item -LiteralPath (Join-Path $repoRoot "docs\$name") -Destination $references -Force }
foreach ($name in @('docs','skills','licenses')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $releaseRoot -Recurse -Force
}
foreach ($name in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md')) { Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $releaseRoot -Force }
New-Item -ItemType Directory -Path (Join-Path $releaseRoot 'examples'),(Join-Path $releaseRoot 'project'),(Join-Path $releaseRoot 'verification') -Force | Out-Null
foreach ($name in @('SerialPlot.crpj','FunctionLab.crpj','RecordPlayback.crpj')) { Copy-Item -LiteralPath (Join-Path $repoRoot "examples\$name") -Destination (Join-Path $releaseRoot 'examples') -Force }
# Ship evidence produced by the CoRig build, keeping historical runs local.
$verificationRoot = Join-Path $repoRoot 'verification\corig-packaged'
$releaseReport = Join-Path $verificationRoot 'report.json'
if (Test-Path -LiteralPath $releaseReport) {
    Copy-Item -LiteralPath $releaseReport -Destination (Join-Path $releaseRoot 'verification\release.json') -Force
}
foreach ($name in @('interface','workbench','instruments','animation','recording','ai')) {
    $report = Join-Path $verificationRoot "$name\report.json"
    if ($name -eq 'interface') { $report = Join-Path $verificationRoot 'interface\result.json' }
    if (Test-Path -LiteralPath $report) {
        Copy-Item -LiteralPath $report -Destination (Join-Path $releaseRoot "verification\$name.json") -Force
    }
}
foreach ($name in @('native','documentation','plugins','installer')) {
    $report = Join-Path $repoRoot "verification\corig-$name\report.json"
    if (Test-Path -LiteralPath $report) {
        Copy-Item -LiteralPath $report -Destination (Join-Path $releaseRoot "verification\$name.json") -Force
    }
}
$pythonFiles = Get-ChildItem -LiteralPath $releaseRoot -Recurse -File | Where-Object { $_.Name -match '^python\d*\.exe$|^python\d*\.dll$|^PySide|^pyqtgraph' -or $_.FullName -match '[\\/]_internal[\\/]' }
if ($pythonFiles) { throw 'The release unexpectedly contains an old Python runtime' }
$publishDirectory = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Path $publishDirectory -Force | Out-Null
$binaryArchive = Join-Path $publishDirectory 'CoRig-Windows-x64.zip'
Compress-Archive -LiteralPath $releaseRoot -DestinationPath $binaryArchive -CompressionLevel Optimal -Force
$sdkArchive = Join-Path $publishDirectory 'CoRig-SDK-Windows-x64.zip'
Compress-Archive -LiteralPath (Join-Path $repoRoot 'build\native\sdk\CoRigSdk') -DestinationPath $sdkArchive -CompressionLevel Optimal -Force

# Corresponding source contains the native modules and vendored libssh2, but
# never local settings, private projects, virtual environments or API tokens.
if (Test-Path -LiteralPath $sourceStage) {
    $resolvedTarget = (Resolve-Path -LiteralPath $sourceStage).Path
    if ($resolvedTarget -ne [IO.Path]::GetFullPath($sourceStage) -or !(Split-Path -Parent $resolvedTarget).Equals([IO.Path]::GetFullPath($outputRoot),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected source staging path' }
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
$sourceRoot = Join-Path $sourceStage 'CoRig'
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
foreach ($name in @('src','python','third_party','tests','tools','docs','skills','sdk','installer','licenses','CMakeLists.txt','build.ps1','CONTEXT.md','README.md','LICENSE','THIRD_PARTY_NOTICES.md','.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $sourceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'examples') -Force | Out-Null
foreach ($name in @('SerialPlot.crpj','FunctionLab.crpj','RecordPlayback.crpj')) { Copy-Item -LiteralPath (Join-Path $repoRoot "examples\$name") -Destination (Join-Path $sourceRoot 'examples') }
Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object { $_.Extension -eq '.pyc' } | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
$sourceArchive = Join-Path $publishDirectory 'CoRig-source.zip'
Compress-Archive -LiteralPath $sourceRoot -DestinationPath $sourceArchive -CompressionLevel Optimal -Force
Get-Item -LiteralPath $binaryArchive,$sdkArchive,$sourceArchive | Select-Object FullName,Length

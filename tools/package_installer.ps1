param(
    [string]$IfwBin = '',
    [string]$RepositoryUrl = 'https://xuyan623.github.io/CoRig/updates/windows-x64/',
    [string]$QtRuntimeVersion = '6.12.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$IfwBin) { $IfwBin = Join-Path $repoRoot '.cache\qt-ifw\Tools\QtInstallerFramework\4.7\bin' }
foreach ($tool in @('binarycreator.exe','repogen.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $IfwBin $tool))) { throw "请通过 -IfwBin 指定 Qt Installer Framework 的 bin 目录：缺少 $tool" }
}
$versionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -Raw),'project\(CoRig VERSION ([0-9.]+)')
if (!$versionMatch.Success) { throw '找不到 CoRig 项目版本' }
$version = $versionMatch.Groups[1].Value
$release = Join-Path $repoRoot 'build\native-release\CoRig'
foreach ($name in @('CoRig.exe','CoRig-cli.exe','CoRigSdk.dll','plugins')) {
    if (!(Test-Path -LiteralPath (Join-Path $release $name))) { throw "请先生成部署目录：缺少 $name" }
}
$stage = Join-Path $repoRoot 'build\installer'
if (Test-Path -LiteralPath $stage) {
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ($resolved -ne [IO.Path]::GetFullPath($stage) -or !(Split-Path -Parent $resolved).Equals((Join-Path $repoRoot 'build'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected installer staging directory' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
$packages = Join-Path $stage 'packages'
$config = Join-Path $stage 'config'
New-Item -ItemType Directory -Path $packages,$config -Force | Out-Null

# Qt dependencies, application files and official plugins are independently
# versioned installer contents, so unchanged Qt files are reused during updates.
$parts = @(
    @{Id='org.corig.runtime';Name='Qt 运行依赖';Version=$QtRuntimeVersion;Description='运行 CoRig 所需的 Qt 与 C++ 运行依赖';Default='true';Dependencies=''},
    @{Id='org.corig.application';Name='联调 CoRig';Version=$version;Description='功能设计、界面设计与调试工作台';Default='true';Dependencies="org.corig.runtime-$QtRuntimeVersion"},
    @{Id='org.corig.official';Name='官方插件';Version=$version;Description='通信、协议、数据处理、数学、仪表与录制回放插件';Default='true';Dependencies='org.corig.application'},
    @{Id='org.corig.sdk';Name='C++ 插件开发 SDK';Version=$version;Description='公开头文件、构建工具与可运行的插件示例';Default='false';Dependencies='org.corig.application'}
)
foreach ($part in $parts) {
    $root = Join-Path $packages $part.Id
    New-Item -ItemType Directory -Path (Join-Path $root 'meta'),(Join-Path $root 'data') -Force | Out-Null
    $extra = ''
    if ($part.Id -eq 'org.corig.application') {
        Copy-Item -LiteralPath (Join-Path $repoRoot 'installer\application\installscript.qs'),(Join-Path $repoRoot 'installer\application\InstallScope.ui'),(Join-Path $repoRoot 'installer\application\UninstallOptions.ui') -Destination (Join-Path $root 'meta')
        $extra = '<Script>installscript.qs</Script><UserInterfaces><UserInterface>InstallScope.ui</UserInterface><UserInterface>UninstallOptions.ui</UserInterface></UserInterfaces>'
    }
    $xml = @"
<?xml version="1.0" encoding="UTF-8"?>
<Package><DisplayName>$($part.Name)</DisplayName><Description>$($part.Description)</Description><Version>$($part.Version)</Version><ReleaseDate>$(Get-Date -Format 'yyyy-MM-dd')</ReleaseDate><Name>$($part.Id)</Name><Default>$($part.Default)</Default><Dependencies>$($part.Dependencies)</Dependencies>$extra</Package>
"@
    Set-Content -LiteralPath (Join-Path $root 'meta\package.xml') -Value $xml -Encoding utf8
}
$applicationFiles = @('CoRig.exe','CoRig-cli.exe','CoRig-uninstall.exe','CoRigSdk.dll','README.md','LICENSE','THIRD_PARTY_NOTICES.md')
$applicationDirs = @('docs','skills','licenses','examples','verification')
foreach ($item in Get-ChildItem -LiteralPath $release -Force) {
    if ($item.Name -in @('portable.ini','project','plugins','.sessions','settings.ini')) { continue }
    $part = if ($item.Name -in $applicationFiles -or $item.Name -in $applicationDirs) { 'org.corig.application' } else { 'org.corig.runtime' }
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $packages "$part\data") -Recurse
}
$pluginData = Join-Path $packages 'org.corig.official\data\plugins'
New-Item -ItemType Directory -Path $pluginData -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $release 'plugins') -Directory | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $pluginData -Recurse}
$sdk = Join-Path $repoRoot 'build\native\sdk\CoRigSdk'
if (!(Test-Path -LiteralPath (Join-Path $sdk 'include\corig\plugin.h'))) { throw '请先构建公开插件 SDK' }
Copy-Item -LiteralPath $sdk -Destination (Join-Path $packages 'org.corig.sdk\data') -Recurse
$xml = Get-Content -LiteralPath (Join-Path $repoRoot 'installer\config\config.xml.in') -Raw
$xml = $xml.Replace('__VERSION__',$version).Replace('__REPOSITORY__',[Security.SecurityElement]::Escape($RepositoryUrl))
Set-Content -LiteralPath (Join-Path $config 'config.xml') -Value $xml -Encoding utf8
$repository = Join-Path $repoRoot 'dist\updates\windows-x64'
if (Test-Path -LiteralPath $repository) {
    $resolved = (Resolve-Path -LiteralPath $repository).Path
    if ($resolved -ne [IO.Path]::GetFullPath($repository) -or !(Split-Path -Parent $resolved).Equals((Join-Path $repoRoot 'dist\updates'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected update repository directory' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path (Split-Path -Parent $repository) -Force | Out-Null
& (Join-Path $IfwBin 'repogen.exe') -p $packages $repository
if ($LASTEXITCODE -ne 0) { throw 'Update repository generation failed' }
$setup = Join-Path $repoRoot "dist\CoRigSetup-$version-Windows-x64.exe"
# Embed all packages while retaining remote maintenance (IFW 4.7 supports this
# without --offline-only; newer releases also provide an explicit hybrid flag).
& (Join-Path $IfwBin 'binarycreator.exe') -c (Join-Path $config 'config.xml') -p $packages $setup
if ($LASTEXITCODE -ne 0) { throw 'Installer generation failed' }
$checksums = @($setup,(Join-Path $repoRoot 'dist\CoRig-Windows-x64.zip'),(Join-Path $repoRoot 'dist\CoRig-SDK-Windows-x64.zip'),(Join-Path $repoRoot 'dist\CoRig-source.zip')) | Where-Object {Test-Path -LiteralPath $_} | ForEach-Object { $hash=Get-FileHash -LiteralPath $_ -Algorithm SHA256; '{0}  {1}' -f $hash.Hash.ToLowerInvariant(),(Split-Path -Leaf $hash.Path) }
Set-Content -LiteralPath (Join-Path $repoRoot 'dist\SHA256SUMS.txt') -Value $checksums -Encoding ascii
Get-Item -LiteralPath $setup,(Join-Path $repository 'Updates.xml') | Select-Object FullName,Length

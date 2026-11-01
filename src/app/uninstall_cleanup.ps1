# Runs from an encoded command in a system process; no temporary script is
# written to C: or left behind inside the installation being removed.
$ErrorActionPreference = 'Stop'
$failures = [Collections.Generic.List[string]]::new()
$root = [IO.Path]::GetFullPath($options.root).TrimEnd('\', '/')
$prefix = $root + [IO.Path]::DirectorySeparatorChar

# Validate the resolved absolute path and every ancestor before any deletion.
# Keep rejects linked program paths. All removes links as directory entries
# and never follows them into external data.
function OwnedPath([string]$relative) {
    if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) {
        throw "Invalid installation entry: $relative"
    }
    $path = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $relative))
    if (!$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Outside installation: $relative" }
    $ancestor = $path
    while ($options.keep -and $ancestor.Length -ge $root.Length) {
        if ([IO.File]::Exists($ancestor) -or [IO.Directory]::Exists($ancestor)) {
            if ([IO.File]::GetAttributes($ancestor) -band [IO.FileAttributes]::ReparsePoint) {
                throw "Retained linked path: $ancestor"
            }
        }
        if ($ancestor -eq $root) { break }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $path
}

# Every recursive target is resolved under the confirmed installation root.
# Directory/file links are unlinked without traversing their external targets.
# A locked entry is reported while other entries continue to be removed.
function RemoveManagedTree([string]$path) {
    $path = [IO.Path]::GetFullPath($path)
    if ($path -ne $root -and !$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Outside installation: $path"
    }
    if (![IO.Directory]::Exists($path)) { return }
    foreach ($entry in Get-ChildItem -LiteralPath $path -Force) {
        try {
            $entryPath = [IO.Path]::GetFullPath($entry.FullName)
            if (!$entryPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Outside installation: $entryPath" }
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                if ($entry.PSIsContainer) { [IO.Directory]::Delete($entryPath, $false) }
                else { [IO.File]::Delete($entryPath) }
            } elseif ($entry.PSIsContainer) { RemoveManagedTree $entryPath }
            else { Remove-Item -LiteralPath $entryPath -Force }
        } catch { $failures.Add($_.Exception.Message) }
    }
    if (!(Get-ChildItem -LiteralPath $path -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $path -Force }
}

try {
    # Opening the actual process object prevents a reused PID from becoming a
    # wait target. No files are removed until validation and acknowledgement.
    $parent = Get-Process -Id $options.parentPid
    $null = $parent.Handle
    if ($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\', '/') -or
        ([IO.File]::GetAttributes($root) -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Installation must be a dedicated physical directory, never a drive root or link'
    }
    $manifestPath = OwnedPath 'install-manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.application -ne 'CoRig' -or $manifest.version -ne 1 -or !$manifest.files) { throw 'Invalid CoRig manifest' }
    $files = @($manifest.files) + @('CoRigMaintenanceTool.exe', 'CoRigMaintenanceTool.dat', 'CoRigMaintenanceTool.ini',
        'components.xml', 'installer.dat', 'InstallationLog.txt', 'network.xml', 'install-manifest.json', 'native.log', 'uninstall-errors.txt')
    foreach ($relative in $files) { $null = OwnedPath $relative }
    foreach ($relative in $manifest.directories) { $null = OwnedPath $relative }
    [IO.File]::WriteAllText($options.ready, 'ready')
    if (!$parent.WaitForExit(60000)) { throw 'Uninstaller did not exit; no files removed' }

    # Stop only processes from this exact installation, never all CoRig copies.
    foreach ($process in Get-CimInstance Win32_Process -Filter "Name='CoRig.exe' OR Name='CoRig-cli.exe' OR Name='CoRigMaintenanceTool.exe'") {
        if ($process.ExecutablePath -and ([IO.Path]::GetDirectoryName($process.ExecutablePath) -eq $root)) {
            $running = Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
            if ($running) { $running.Kill(); $running.WaitForExit() }
        }
    }
    if (!$options.keep) {
        RemoveManagedTree $root
    } else {
        $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($relative in $manifest.directories) {
            if ($options.keep -and ($relative -eq 'plugins' -or $relative -match '^plugins[\\/]')) { continue }
            $null = $directories.Add((OwnedPath $relative))
        }
        foreach ($relative in ($files | Select-Object -Unique)) {
            if ($options.keep -and ($relative -match '^plugins[\\/]')) { continue }
            try {
                $path = OwnedPath $relative
                if (Test-Path -LiteralPath $path) {
                    if ([IO.Directory]::Exists($path)) { throw "Expected program file: $relative" }
                    Remove-Item -LiteralPath $path -Force
                }
                $directory = [IO.Path]::GetDirectoryName($path)
                while ($directory.Length -gt $root.Length) {
                    $null = $directories.Add($directory)
                    $directory = [IO.Path]::GetDirectoryName($directory)
                }
            } catch { $failures.Add($_.Exception.Message) }
        }
        $managed = @('cache', '.sessions')
        foreach ($relative in $managed) {
            try { RemoveManagedTree (OwnedPath $relative) } catch { $failures.Add($_.Exception.Message) }
        }
        foreach ($directory in ($directories | Sort-Object Length -Descending)) {
            try {
                $null = OwnedPath $directory.Substring($prefix.Length)
                if ((Test-Path -LiteralPath $directory) -and !(Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {
                    Remove-Item -LiteralPath $directory -Force
                }
            } catch { $failures.Add($_.Exception.Message) }
        }
    }

    # Delete only shortcuts and registration entries that point at this copy.
    $shell = New-Object -ComObject WScript.Shell
    $menu = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\CoRig'
    if (Test-Path -LiteralPath $menu) {
        foreach ($link in Get-ChildItem -LiteralPath $menu -Filter '*.lnk' -File) {
            if ($shell.CreateShortcut($link.FullName).TargetPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $link.FullName -Force
            }
        }
        if (!(Get-ChildItem -LiteralPath $menu -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $menu }
    }
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    foreach ($key in Get-ChildItem -LiteralPath $uninstallKey -ErrorAction SilentlyContinue) {
        $entry = Get-ItemProperty -LiteralPath $key.PSPath
        if ($entry.InstallLocation -and ([string]$entry.InstallLocation).Replace('/','\').TrimEnd('\') -eq $root) {
            Remove-Item -LiteralPath $key.PSPath -Recurse -Force
        }
    }
    foreach ($association in @(@('.crpj', 'CoRig.Project'), @('.crpl', 'CoRig.Plugin'))) {
        $key = 'HKCU:\Software\Classes\' + $association[1]
        $command = Get-Item -LiteralPath ($key + '\shell\open\command') -ErrorAction SilentlyContinue
        if ($command -and $command.GetValue('').Replace('/','\').IndexOf($root + '\CoRig.exe', [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Remove-Item -LiteralPath $key -Recurse -Force
            $extension = 'HKCU:\Software\Classes\' + $association[0]
            $entry = Get-Item -LiteralPath $extension -ErrorAction SilentlyContinue
            if ($entry -and $entry.GetValue('') -eq $association[1]) {
                $writable = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(('Software\Classes\' + $association[0]), $true)
                try { $writable.DeleteValue('') } finally { $writable.Dispose() }
                $entry = Get-Item -LiteralPath $extension
                if (!$entry.ValueCount -and !$entry.SubKeyCount) { Remove-Item -LiteralPath $extension }
            }
        }
    }
} catch { $failures.Add($_.Exception.Message) }

# All succeeds only if the confirmed installation directory is gone. Keep
# reports retained user data and additional files after removing the program.
$retained = @()
if (Test-Path -LiteralPath $root) {
    $retained = @(Get-ChildItem -LiteralPath $root -Force | Select-Object -ExpandProperty Name)
    if (!$retained.Count -and !$failures.Count) {
        try { Remove-Item -LiteralPath $root -Force } catch { $failures.Add($_.Exception.Message) }
    }
}
if (!$options.keep -and (Test-Path -LiteralPath $root) -and !$failures.Count) {
    $failures.Add("Installation directory still contains files: $root")
}
$result = @{ success = ($failures.Count -eq 0); failures = @($failures.ToArray()); retained = $retained; root = $root; keep = $options.keep }
if ($options.report) { $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $options.report -Encoding UTF8 }
if ($failures.Count -and (Test-Path -LiteralPath $root)) {
    $failures | Set-Content -LiteralPath (Join-Path $root 'uninstall-errors.txt') -Encoding UTF8
}
if ($options.interactive) {
    $shell = New-Object -ComObject WScript.Shell
    if ($failures.Count) { $null = $shell.Popup(('部分内容未能移除：' + "`n" + ($failures -join "`n")), 0, 'CoRig 卸载结果', 48) }
    elseif ($retained.Count) { $null = $shell.Popup(('CoRig 程序已移除。按所选范围保留以下内容：' + "`n" + ($retained -join "`n")), 0, 'CoRig 卸载完成', 64) }
    else { $null = $shell.Popup('CoRig 程序与个人内容已移除，安装目录已删除。', 0, 'CoRig 卸载完成', 64) }
}
if ($failures.Count) { exit 2 }

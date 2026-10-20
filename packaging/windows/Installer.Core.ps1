# Shared by the ZIP packager, Windows installer, and filesystem tests.
Set-StrictMode -Version 2.0

function Write-ReplayZip {
    param([string]$Source, [string]$Destination)
    # Compress-Archive interprets brackets in destination folders as wildcards
    # even with -LiteralPath. The .NET ZIP API also includes hidden files.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory([IO.Path]::GetFullPath($Source), [IO.Path]::GetFullPath($Destination),
                                                [IO.Compression.CompressionLevel]::Optimal, $false)
}

function Assert-ReplayWindowsBinary {
    param([string]$Path, [ValidateSet("Dll", "Exe")][string]$Kind)
    $Stream = [IO.File]::OpenRead($Path)
    $Reader = New-Object IO.BinaryReader($Stream)
    try {
        if ($Stream.Length -lt 64 -or $Reader.ReadUInt16() -ne 0x5A4D) { throw "Not a Windows binary: $Path" }
        $Stream.Position = 60
        $Header = $Reader.ReadUInt32()
        if ($Header -gt $Stream.Length - 24) { throw "Invalid Windows binary header: $Path" }
        $Stream.Position = $Header
        $Signature = $Reader.ReadUInt32()
        $Machine = $Reader.ReadUInt16()
        if ($Signature -ne 0x4550 -or $Machine -ne 0x8664) {
            throw "A Windows x64 binary is required: $Path"
        }
        $Stream.Position = $Header + 22
        $IsDll = ($Reader.ReadUInt16() -band 0x2000) -ne 0
        if (($Kind -eq "Dll") -ne $IsDll) { throw "Expected a Windows $Kind file: $Path" }
    } finally { $Reader.Dispose() }
}

function Get-ReplayPayloadFile {
    param([string]$Payload, [string]$RelativePath)
    $Relative = $RelativePath.Replace('\', '/')
    if (!$Relative -or $Relative.StartsWith('/') -or $Relative.Contains(':') -or $Relative -match '(^|/)\.\.(/|$)') {
        throw "Invalid package file path: $RelativePath"
    }
    $Root = [IO.Path]::GetFullPath($Payload).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $Path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (!$Path.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) { throw "Package file is outside its payload" }
    return $Path
}

function Assert-ReplayPackage {
    param([string]$PackageRoot)
    $Manifest = Get-Content -LiteralPath (Join-Path $PackageRoot "package.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Manifest.plugin -ne "obs-multi-replay" -or $Manifest.platform -ne "windows-x64" -or $Manifest.format -ne 1) {
        throw "This is not an OBS Multi-Length Replay Windows x64 installer package."
    }
    $Payload = Join-Path $PackageRoot "payload/obs-multi-replay"
    Assert-ReplaySafePath $Payload
    if ($Manifest.version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid plugin version" }
    foreach ($Item in Get-ChildItem -LiteralPath $Payload -Recurse -Force) {
        if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Links are not allowed in the payload" }
    }
    $Paths = @{}
    foreach ($File in $Manifest.files) {
        $Path = Get-ReplayPayloadFile $Payload $File.path
        if ($Paths.ContainsKey($File.path)) { throw "Duplicate package file: $($File.path)" }
        $Paths[$File.path] = $true
        if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $File.sha256) {
            throw "Package file is missing or damaged: $($File.path). Extract the full ZIP again."
        }
    }
    foreach ($Required in @("bin/64bit/obs-multi-replay.dll", "data/multi-replay-writer.exe", "data/locale/en-US.ini")) {
        if (!$Paths.ContainsKey($Required)) { throw "Package is missing $Required. A Windows build is required." }
    }
    # Unlisted files must not be copied into OBS by a damaged or modified ZIP.
    $Root = [IO.Path]::GetFullPath($Payload).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    foreach ($File in Get-ChildItem -LiteralPath $Payload -Recurse -File -Force) {
        $Relative = $File.FullName.Substring($Root.Length).Replace('\', '/')
        if (!$Paths.ContainsKey($Relative)) { throw "Unlisted package file: $Relative" }
    }
    Assert-ReplayWindowsBinary (Join-Path $Payload "bin/64bit/obs-multi-replay.dll") Dll
    Assert-ReplayWindowsBinary (Join-Path $Payload "data/multi-replay-writer.exe") Exe
    return $Manifest
}

function Assert-ReplayObsStopped {
    if (@(Get-Process -Name obs64, obs32, obs -ErrorAction SilentlyContinue).Count -gt 0) {
        throw "Close OBS before installing or removing the plugin, then run this installer again."
    }
}

function Assert-ReplaySafePath {
    param([string]$Path)
    $Current = [IO.Path]::GetFullPath($Path)
    while ($Current) {
        if (Test-Path -LiteralPath $Current) {
            if ((Get-Item -LiteralPath $Current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a linked installation path: $Current"
            }
        }
        $Current = Split-Path -Parent $Current
    }
}

function Write-ReplayJournal {
    param([string]$Path, [object]$Record)
    Assert-ReplaySafePath $Path
    $TempPath = $Path + ".tmp"
    Assert-ReplaySafePath $TempPath
    [IO.File]::WriteAllText($TempPath, ($Record | ConvertTo-Json -Depth 8), [Text.Encoding]::UTF8)
    # File.Replace is atomic on Windows; a new journal uses a same-volume rename.
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($TempPath, $Path, [NullString]::Value) }
    else { [IO.File]::Move($TempPath, $Path) }
}

function Restore-ReplayTransaction {
    param([string]$JournalPath, [object[]]$Targets)
    Assert-ReplaySafePath $JournalPath
    if (!(Test-Path -LiteralPath $JournalPath)) { return }
    $Record = Get-Content -LiteralPath $JournalPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $Allowed = @($Targets | ForEach-Object { [IO.Path]::GetFullPath($_.Path) })
    foreach ($Entry in $Record.entries) {
        if ($Allowed -notcontains $Entry.Path) { throw "Recovery targets differ. Rerun the previous installation mode/folder." }
        foreach ($Field in @("Stage", "Backup")) {
            $Path = $Entry.$Field
            if ((Split-Path -Parent $Path) -ne (Split-Path -Parent $Entry.Path) -or
                (Split-Path -Leaf $Path) -notmatch '^\.obs-multi-replay-(stage|backup)-[a-f0-9]{32}$') {
                throw "Invalid installation recovery metadata"
            }
            Assert-ReplaySafePath $Path
        }
        Assert-ReplaySafePath $Entry.Path
    }
    for ($Index = $Record.entries.Count - 1; $Index -ge 0; --$Index) {
        $Entry = $Record.entries[$Index]
        if (!$Record.committed) {
            if (Test-Path -LiteralPath $Entry.Backup) {
                if (Test-Path -LiteralPath $Entry.Path) { Remove-Item -LiteralPath $Entry.Path -Recurse -Force }
                Move-Item -LiteralPath $Entry.Backup -Destination $Entry.Path
            } elseif (!$Entry.HadOriginal -and (Test-Path -LiteralPath $Entry.Path)) {
                Remove-Item -LiteralPath $Entry.Path -Recurse -Force
            }
        }
        foreach ($Path in @($Entry.Stage, $Entry.Backup)) {
            if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
        }
    }
    Remove-Item -LiteralPath $JournalPath -Force
}

function Set-ReplayInstallTargets {
    param([object[]]$Targets, [string]$JournalPath = "")
    if (!$JournalPath) { $JournalPath = Join-Path (Split-Path -Parent $Targets[0].Path) ".obs-multi-replay-transaction.json" }
    Restore-ReplayTransaction $JournalPath $Targets
    $Entries = New-Object 'Collections.Generic.List[object]'
    $Committed = $false
    try {
        foreach ($Target in $Targets) {
            Assert-ReplaySafePath $Target.Path
            if (!$Target.Source -and !(Test-Path -LiteralPath $Target.Path)) { continue }
            $Parent = Split-Path -Parent $Target.Path
            [IO.Directory]::CreateDirectory($Parent) | Out-Null
            $Id = [Guid]::NewGuid().ToString("N")
            $Entry = [PSCustomObject]@{
                Path = [IO.Path]::GetFullPath($Target.Path)
                Stage = Join-Path $Parent ".obs-multi-replay-stage-$Id"
                Backup = Join-Path $Parent ".obs-multi-replay-backup-$Id"
                HasSource = [bool]$Target.Source
                HadOriginal = (Test-Path -LiteralPath $Target.Path)
            }
            $Entries.Add($Entry)
            if ($Entry.HasSource) { Copy-Item -LiteralPath $Target.Source -Destination $Entry.Stage -Recurse -Force }
        }
        if (!$Entries.Count) { return }
        $Record = @{ committed = $false; entries = @($Entries.ToArray()) }
        Write-ReplayJournal $JournalPath $Record
        Assert-ReplayObsStopped
        foreach ($Entry in $Entries) {
            if ($Entry.HadOriginal) { Move-Item -LiteralPath $Entry.Path -Destination $Entry.Backup }
            if ($Entry.HasSource) { Move-Item -LiteralPath $Entry.Stage -Destination $Entry.Path }
        }
        $Record.committed = $true
        Write-ReplayJournal $JournalPath $Record
        $Committed = $true
    } catch {
        $Failure = $_
        try { Restore-ReplayTransaction $JournalPath $Targets }
        catch { throw "$($Failure.Exception.Message)`nRecovery files retained at $JournalPath : $($_.Exception.Message)" }
        throw $Failure
    } finally {
        foreach ($Entry in $Entries) {
            if (Test-Path -LiteralPath $Entry.Stage) { Remove-Item -LiteralPath $Entry.Stage -Recurse -Force }
        }
    }
    if ($Committed) {
        try { Restore-ReplayTransaction $JournalPath $Targets }
        catch { Write-Warning "Installation completed; cleanup will be retried next run: $($_.Exception.Message)" }
    }
}

function Find-ReplayObs {
    $Candidates = @()
    foreach ($Key in @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio",
                       "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio")) {
        $Item = Get-ItemProperty -LiteralPath $Key -ErrorAction SilentlyContinue
        if ($Item -and $Item.PSObject.Properties["InstallLocation"]) { $Candidates += $Item.InstallLocation }
    }
    $Candidates += Join-Path $env:ProgramFiles "obs-studio"
    foreach ($Path in $Candidates) {
        if ($Path -and (Test-Path -LiteralPath (Join-Path $Path "bin/64bit/obs64.exe"))) { return $Path }
    }
    return ""
}

function Get-ReplayInstallation {
    param([string]$ObsRoot, [string]$Mode, [string]$Version)
    if ($Mode -eq "Standard") {
        $Path = Join-Path $env:ProgramData "obs-studio/plugins/obs-multi-replay"
        $Receipt = Join-Path $Path "data/replay-installation.json"
        $Exists = (Test-Path -LiteralPath $Path) -or
                  ($ObsRoot -and (Test-Path -LiteralPath (Join-Path $ObsRoot "obs-plugins/64bit/obs-multi-replay.dll")))
    } else {
        $Path = $ObsRoot
        $Receipt = Join-Path $Path "data/obs-plugins/obs-multi-replay/replay-installation.json"
        $Exists = Test-Path -LiteralPath (Join-Path $Path "obs-plugins/64bit/obs-multi-replay.dll")
    }
    $Current = ""
    if (Test-Path -LiteralPath $Receipt) {
        $Info = Get-Content -LiteralPath $Receipt -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($Info.version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid installed version metadata" }
        $Current = $Info.version
        if ([version]$Current -gt [version]$Version) { throw "Version $Current is installed. Downgrading to $Version is blocked." }
    }
    $Action = "Install"
    if ($Exists) { $Action = "Update" }
    if ($Current -eq $Version) { $Action = "Repair" }
    return [PSCustomObject]@{ action = $Action; installed_version = $Current; destination = $Path }
}

function Invoke-ReplaySetup {
    param([string]$Action, [string]$Mode, [string]$ObsRoot, [string]$PackageRoot)
    $Mutex = New-Object Threading.Mutex($false, "Global\OBS-Multi-Length-Replay-Installer")
    $Acquired = $false
    $Temporary = $null
    try {
        try { $Acquired = $Mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $Acquired = $true }
        if (!$Acquired) { throw "Another plugin installer is already running" }
        Assert-ReplayObsStopped
        if (!$ObsRoot -and $Mode -eq "Standard") { $ObsRoot = Find-ReplayObs }
        $Installing = $Action -ne "Uninstall"
        if ($Installing) {
            $Package = Assert-ReplayPackage $PackageRoot
            if (!$ObsRoot) { throw "Select the OBS Studio folder containing bin\64bit\obs64.exe" }
            $ObsRoot = [IO.Path]::GetFullPath($ObsRoot)
            $Exe = Join-Path $ObsRoot "bin/64bit/obs64.exe"
            Assert-ReplayWindowsBinary $Exe Exe
            $ObsVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($Exe).ProductVersion
            if ($ObsVersion -notmatch '(\d+\.\d+(?:\.\d+){0,2})' -or [version]$Matches[1] -lt [version]$Package.minimum_obs) {
                throw "OBS Studio $($Package.minimum_obs) or newer (x64) is required"
            }
            $Info = Get-ReplayInstallation $ObsRoot $Mode $Package.version
            if ($Action -eq "Inspect") {
                return "$($Info.action) version $($Package.version)`r`nInstalled: $(if ($Info.installed_version) { $Info.installed_version } elseif ($Info.action -eq 'Update') { 'older script installation' } else { 'not installed' })`r`nOBS: $ObsRoot`r`nDestination: $($Info.destination)"
            }
        }
        $Targets = @()
        $Payload = ""
        if ($Installing) {
            $Temporary = Join-Path ([IO.Path]::GetTempPath()) ("obs-replay-payload-" + [Guid]::NewGuid().ToString("N"))
            Copy-Item -LiteralPath (Join-Path $PackageRoot "payload/obs-multi-replay") -Destination $Temporary -Recurse
            @{ version = $Package.version; mode = $Mode } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Temporary "data/replay-installation.json") -Encoding UTF8
            $Payload = $Temporary
        }
        if ($Mode -eq "Standard") {
            $Path = Join-Path $env:ProgramData "obs-studio/plugins/obs-multi-replay"
            $Targets += @{ Path = $Path; Source = $Payload }
            if ($ObsRoot) {
                $Targets += @{ Path = (Join-Path $ObsRoot "obs-plugins/64bit/obs-multi-replay.dll"); Source = $null }
                $Targets += @{ Path = (Join-Path $ObsRoot "data/obs-plugins/obs-multi-replay"); Source = $null }
            }
            $Journal = Join-Path (Split-Path -Parent $Path) ".obs-multi-replay-transaction.json"
        } else {
            if (!$ObsRoot) { throw "A portable OBS root is required" }
            $Targets += @{ Path = (Join-Path $ObsRoot "obs-plugins/64bit/obs-multi-replay.dll"); Source = $(if ($Installing) { Join-Path $Payload "bin/64bit/obs-multi-replay.dll" } else { $null }) }
            $Targets += @{ Path = (Join-Path $ObsRoot "data/obs-plugins/obs-multi-replay"); Source = $(if ($Installing) { Join-Path $Payload "data" } else { $null }) }
            $Journal = Join-Path $ObsRoot ".obs-multi-replay-transaction.json"
        }
        Set-ReplayInstallTargets $Targets $Journal
        try { Remove-ReplayLegacySetup $Mode $ObsRoot }
        catch { throw "Plugin files were changed successfully, but old installer cleanup failed. Do not run the old uninstaller. $($_.Exception.Message)" }
        return "Complete. Your OBS settings, hotkeys and recordings are preserved."
    } finally {
        if ($Temporary -and (Test-Path -LiteralPath $Temporary)) { Remove-Item -LiteralPath $Temporary -Recurse -Force }
        if ($Acquired) { $Mutex.ReleaseMutex() }
        $Mutex.Dispose()
    }
}

function Test-ReplayWritablePath {
    param([string]$Path)
    Assert-ReplaySafePath $Path
    $Parent = [IO.Path]::GetFullPath($Path)
    while (!(Test-Path -LiteralPath $Parent -PathType Container)) { $Parent = Split-Path -Parent $Parent }
    $Probe = Join-Path $Parent (".obs-replay-permission-" + [Guid]::NewGuid().ToString("N"))
    try { [IO.File]::WriteAllText($Probe, ""); return $true }
    catch [UnauthorizedAccessException] { return $false }
    catch [IO.IOException] { return $false }
    finally { if (Test-Path -LiteralPath $Probe) { Remove-Item -LiteralPath $Probe -Force } }
}

function Remove-ReplayLegacySetup {
    param([string]$Mode, [string]$ObsRoot)
    # Never execute the obsolete uninstaller: it would remove the new payload.
    if ($Mode -eq "Standard") { $Support = Join-Path $env:ProgramFiles "OBS Multi-Length Replay" }
    else { $Support = Join-Path $ObsRoot "obs-multi-replay-setup" }
    Assert-ReplaySafePath $Support
    $Owned = $false
    $Ini = Join-Path $Support "installation.ini"
    if (Test-Path -LiteralPath $Ini) {
        $Text = Get-Content -LiteralPath $Ini -Raw
        $Owned = $Text -match ('(?im)^Mode=' + [regex]::Escape($Mode) + '\s*$') -and $Text -match '(?im)^\[Setup\]\s*$'
    }
    $Key = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS-Multi-Length-Replay_is1"
    $RegistrationOwned = $false
    if ($Mode -eq "Standard" -and $env:OS -eq "Windows_NT" -and (Test-Path -LiteralPath $Key)) {
        $Item = Get-ItemProperty -LiteralPath $Key
        if ($Item.PSObject.Properties['DisplayName'] -and $Item.PSObject.Properties['UninstallString']) {
            $Expected = Join-Path $Support "unins000.exe"
            $RegistrationOwned = $Item.DisplayName -eq "OBS Multi-Length Replay" -and $Item.UninstallString.Trim('"') -eq $Expected
        }
        if ($RegistrationOwned) { $Owned = $true }
        else { Write-Warning "Unrecognized old uninstall registration retained: $Key" }
    }
    if ((Test-Path -LiteralPath $Support) -and !$Owned) {
        Write-Warning "Unrecognized old installer files retained: $Support. Use these scripts, not the old uninstaller."
        return
    }
    if ($Owned) {
        foreach ($Name in @("Installer.Core.ps1", "Setup.Backend.ps1", "installation.ini", "unins000.exe", "unins000.dat", "unins000.msg")) {
            $Path = Join-Path $Support $Name
            Assert-ReplaySafePath $Path
            if (Test-Path -LiteralPath $Path -PathType Leaf) { Remove-Item -LiteralPath $Path -Force }
        }
        if (Test-Path -LiteralPath $Support) {
            if (@(Get-ChildItem -LiteralPath $Support -Force).Count -eq 0) { Remove-Item -LiteralPath $Support }
            else { Write-Warning "Unfamiliar files retained in $Support" }
        }
        if ($RegistrationOwned) { Remove-Item -LiteralPath $Key -Force }
    }
}

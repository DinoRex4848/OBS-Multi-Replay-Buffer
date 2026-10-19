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
        if ($Reader.ReadUInt32() -ne 0x4550 -or $Reader.ReadUInt16() -ne 0x8664) {
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

function Set-ReplayInstallTargets {
    param([object[]]$Targets)
    # Stage all copies before changing any installed file. Each rename stays
    # on the destination drive. A failure restores the previous installation.
    $Entries = New-Object 'Collections.Generic.List[object]'
    $Success = $false
    try {
        foreach ($Target in $Targets) {
            $Parent = Split-Path -Parent $Target.Path
            [IO.Directory]::CreateDirectory($Parent) | Out-Null
            $Id = [Guid]::NewGuid().ToString("N")
            $Entry = [PSCustomObject]@{
                Path = $Target.Path
                Stage = Join-Path $Parent ".obs-multi-replay-stage-$Id"
                Backup = Join-Path $Parent ".obs-multi-replay-backup-$Id"
                HasSource = [bool]$Target.Source
                OldMoved = $false
                NewMoved = $false
            }
            $Entries.Add($Entry)
            if ($Entry.HasSource) { Copy-Item -LiteralPath $Target.Source -Destination $Entry.Stage -Recurse -Force }
        }
        foreach ($Entry in $Entries) {
            if (Test-Path -LiteralPath $Entry.Path) {
                Move-Item -LiteralPath $Entry.Path -Destination $Entry.Backup
                $Entry.OldMoved = $true
            }
            if ($Entry.HasSource) {
                Move-Item -LiteralPath $Entry.Stage -Destination $Entry.Path
                $Entry.NewMoved = $true
            }
        }
        $Success = $true
    } catch {
        $Failure = $_
        $RestoreErrors = @()
        for ($Index = $Entries.Count - 1; $Index -ge 0; --$Index) {
            $Entry = $Entries[$Index]
            try {
                if ($Entry.NewMoved) { Remove-Item -LiteralPath $Entry.Path -Recurse -Force }
                if ($Entry.OldMoved) { Move-Item -LiteralPath $Entry.Backup -Destination $Entry.Path }
            } catch { $RestoreErrors += "Previous files are at $($Entry.Backup): $($_.Exception.Message)" }
        }
        if ($RestoreErrors.Count) { throw "$($Failure.Exception.Message)`n$($RestoreErrors -join '`n')" }
        throw $Failure
    } finally {
        foreach ($Entry in $Entries) {
            if (Test-Path -LiteralPath $Entry.Stage) { Remove-Item -LiteralPath $Entry.Stage -Recurse -Force }
            if ($Success -and (Test-Path -LiteralPath $Entry.Backup)) {
                Remove-Item -LiteralPath $Entry.Backup -Recurse -Force
            }
        }
    }
}

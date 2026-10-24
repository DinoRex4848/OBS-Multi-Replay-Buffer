# Run with PowerShell 5.1+; tests stay in a temporary directory and never run
# fixture binaries or touch real OBS folders, profiles, or registry entries.
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $Repo "packaging/windows/Installer.Core.ps1")

function Check {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw $Message }
}
function Expect-Failure {
    param([scriptblock]$Operation, [string]$Text)
    try { & $Operation | Out-Null } catch {
        Check ($_.Exception.Message.Contains($Text)) "Unexpected failure: $($_.Exception.Message)"
        return
    }
    throw "Expected failure containing: $Text"
}
function Write-FixtureBinary {
    param([string]$Path, [bool]$Dll, [string]$Label, [uint16]$Machine = 0x8664)
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    $Bytes = New-Object byte[] 256
    $Bytes[0] = 0x4D; $Bytes[1] = 0x5A
    [Array]::Copy([BitConverter]::GetBytes([uint32]128), 0, $Bytes, 60, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]0x4550), 0, $Bytes, 128, 4)
    [Array]::Copy([BitConverter]::GetBytes($Machine), 0, $Bytes, 132, 2)
    if ($Dll) { $Bytes[151] = 0x20 }
    $Text = [Text.Encoding]::UTF8.GetBytes($Label)
    [Array]::Copy($Text, 0, $Bytes, 192, $Text.Length)
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

$Temp = Join-Path ([IO.Path]::GetTempPath()) ("obs-replay-installer-tests-" + [Guid]::NewGuid().ToString("N"))
[IO.Directory]::CreateDirectory($Temp) | Out-Null
try {
    # Parse every shipped script too; catches syntax errors before a Windows build.
    $Scripts = @(Get-ChildItem -LiteralPath (Join-Path $Repo "packaging/windows") -Filter *.ps1)
    $Scripts += Get-Item -LiteralPath (Join-Path $Repo "scripts/package-windows.ps1")
    $Scripts += Get-Item -LiteralPath (Join-Path $Repo "scripts/build-windows.ps1")
    $Scripts += Get-Item -LiteralPath (Join-Path $Repo "tests/windows_package_tests.ps1")
    foreach ($Script in $Scripts) {
        $Tokens = $null; $Errors = $null
        [Management.Automation.Language.Parser]::ParseFile($Script.FullName, [ref]$Tokens, [ref]$Errors) | Out-Null
        Check ($Errors.Count -eq 0) "Parse error in $($Script.FullName): $Errors"
    }
    $Stage = Join-Path $Temp "fixture-stage"
    $Dll = Join-Path $Stage "obs-plugins/64bit/obs-multi-replay.dll"
    $Data = Join-Path $Stage "data/obs-plugins/obs-multi-replay"
    Write-FixtureBinary $Dll $true "FIXTURE DLL - not executable"
    Write-FixtureBinary (Join-Path $Data "multi-replay-writer.exe") $false "FIXTURE EXE - not executable"
    Write-FixtureBinary (Join-Path $Data "avformat-61.dll") $true "FIXTURE dependency"
    [IO.Directory]::CreateDirectory((Join-Path $Data "locale")) | Out-Null
    [IO.File]::WriteAllText((Join-Path $Data "locale/en-US.ini"), "Test fixture locale")
    $Out = Join-Path $Temp "Friend's [package] & output"
    & (Join-Path $Repo "scripts/package-windows.ps1") -PackagePath $Stage -OutputDirectory $Out
    $ArchiveRoot = Join-Path $Out "windows-installer"
    $Kit = Join-Path $ArchiveRoot "PluginFiles"
    $Payload = Join-Path $Kit "payload/obs-multi-replay"
    Assert-ReplayPackage $Kit | Out-Null
    # Detection must skip stale paths and accept a custom registered OBS folder.
    $DetectedRoot = Join-Path $Temp "Custom OBS"
    [IO.Directory]::CreateDirectory((Join-Path $DetectedRoot "bin/64bit")) | Out-Null
    [IO.File]::WriteAllText((Join-Path $DetectedRoot "bin/64bit/obs64.exe"), "detection fixture only")
    $global:ReplayDetectionRoot = $DetectedRoot
    function Get-ReplayObsCandidates { @("/missing-obsolete-obs", $global:ReplayDetectionRoot) }
    Check ((Find-ReplayObs) -eq $DetectedRoot) "Custom OBS detection failed"
    Remove-Item Function:\Get-ReplayObsCandidates
    . (Join-Path $Repo "packaging/windows/Installer.Core.ps1")
    $PackageVersion = (Assert-ReplayPackage $Kit).version
    $ZipPath = Join-Path $Out "obs-multi-replay-$PackageVersion-windows-x64.zip"
    Check (Test-Path -LiteralPath $ZipPath) "Installer ZIP missing"
    $Zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $Roots = @($Zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/').Split('/')[0] } | Sort-Object -Unique)
        Check (($Roots -join ',') -eq "install.bat,PluginFiles,README.txt,uninstall.bat") "ZIP must contain exactly three files and PluginFiles"
        # Windows PowerShell may use backslashes in ZIP entries; compare logical paths.
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "install.bat" }).Count -eq 1) "ZIP launcher missing"
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "PluginFiles/payload/obs-multi-replay/data/multi-replay-writer.exe" }).Count -eq 1) "ZIP helper missing"
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "PluginFiles/package.json" }).Count -eq 1) "ZIP manifest missing"
    } finally { $Zip.Dispose() }
    Check (Test-Path -LiteralPath (Join-Path $ArchiveRoot "install.bat")) "Launcher missing"
    $ManifestPath = Join-Path $Kit "package.json"
    $ManifestText = Get-Content -LiteralPath $ManifestPath -Raw
    $Installed = Join-Path $Temp "Friend's [OBS] & Files/plugins/obs-multi-replay"
    $Profile = Join-Path $Temp "profiles/basic.ini"
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Profile)) | Out-Null
    [IO.File]::WriteAllText($Profile, "DO NOT CHANGE PROFILE")
    $Preserved = @{}
    foreach ($Name in @("config/obs-studio/basic/scenes/Friends.json", "config/obs-studio/hotkeys.ini", "recordings/keep.mkv")) {
        $Path = Join-Path (Join-Path $Temp "portable OBS") $Name
        [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
        [IO.File]::WriteAllText($Path, "Preserve $Name")
        $Preserved[$Path] = (Get-FileHash -LiteralPath $Path).Hash
    }
    $Other = Join-Path (Split-Path -Parent $Installed) "another-plugin/keep.txt"
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Other)) | Out-Null
    [IO.File]::WriteAllText($Other, "DO NOT CHANGE OTHER PLUGIN")
    Set-ReplayInstallTargets @(@{ Path = $Installed; Source = $Payload })
    Check ((Get-FileHash -LiteralPath (Join-Path $Installed "bin/64bit/obs-multi-replay.dll")).Hash -eq
           (Get-FileHash -LiteralPath $Dll).Hash) "Fresh install DLL differs"
    [IO.File]::WriteAllText((Join-Path $Installed "data/unused-old-file.txt"), "stale")
    Set-ReplayInstallTargets @(@{ Path = $Installed; Source = $Payload })
    Check (!(Test-Path -LiteralPath (Join-Path $Installed "data/unused-old-file.txt"))) "Update retained stale payload"
    $Helper = Join-Path $Payload "data/multi-replay-writer.exe"
    $Original = [IO.File]::ReadAllBytes($Helper)
    [IO.File]::AppendAllText($Helper, "tampered")
    Expect-Failure { Assert-ReplayPackage $Kit } "missing or damaged"
    [IO.File]::WriteAllBytes($Helper, $Original)
    $Manifest = $ManifestText | ConvertFrom-Json
    $Manifest.files[0].path = "../outside.txt"
    $Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ManifestPath
    Expect-Failure { Assert-ReplayPackage $Kit } "Invalid package file path"
    [IO.File]::WriteAllText($ManifestPath, $ManifestText)
    [IO.File]::WriteAllText((Join-Path $Payload "data/unlisted.txt"), "extra")
    Expect-Failure { Assert-ReplayPackage $Kit } "Unlisted package file"
    Remove-Item -LiteralPath (Join-Path $Payload "data/unlisted.txt")
    [IO.File]::WriteAllText((Join-Path $Payload "data/.hidden-extra.txt"), "extra")
    Expect-Failure { Assert-ReplayPackage $Kit } "Unlisted package file"
    Remove-Item -LiteralPath (Join-Path $Payload "data/.hidden-extra.txt") -Force
    # OneDrive cloud reparse points are allowed; symlinks and junctions stay refused.
    $LinkTarget = Join-Path $Temp "link target"
    [IO.Directory]::CreateDirectory($LinkTarget) | Out-Null
    foreach ($Link in @((Join-Path $Temp "linked folder"), (Join-Path $Payload "data/linked"))) {
        if ($env:OS -eq "Windows_NT") { New-Item -ItemType Junction -Path $Link -Value $LinkTarget | Out-Null }
        else { New-Item -ItemType SymbolicLink -Path $Link -Value $LinkTarget | Out-Null }
    }
    Expect-Failure { Assert-ReplaySafePath (Join-Path $Temp "linked folder/plugins") } "Refusing a linked installation path"
    Expect-Failure { Assert-ReplayPackage $Kit } "Links are not allowed"
    if ($env:OS -eq "Windows_NT") {
        Check ((Get-ReplayReparseTag (Join-Path $Temp "linked folder")) -eq [Convert]::ToUInt32("A0000003", 16)) "Junction tag not read"
        foreach ($Tag in @("9000001A", "9000101A", "9000F01A")) {
            Check ([ReplayReparseTag]::IsCloud([Convert]::ToUInt32($Tag, 16))) "OneDrive tag $Tag refused"
        }
        foreach ($Tag in @("A000000C", "A0000003", "8000001B", "0")) {
            Check (![ReplayReparseTag]::IsCloud([Convert]::ToUInt32($Tag, 16))) "Tag $Tag allowed as a cloud file"
        }
    }
    # Directory.Delete removes the link itself, never its target's contents.
    foreach ($Link in @((Join-Path $Temp "linked folder"), (Join-Path $Payload "data/linked"))) { [IO.Directory]::Delete($Link) }
    Assert-ReplayPackage $Kit | Out-Null
    $BadBinary = Join-Path $Temp "x86.dll"
    Write-FixtureBinary $BadBinary $true "x86 fixture" 0x14C
    Expect-Failure { Assert-ReplayWindowsBinary $BadBinary Dll } "Windows x64"
    Expect-Failure { Assert-ReplayWindowsBinary $Dll Exe } "Expected a Windows Exe"

    $Portable = Join-Path $Temp "portable OBS"
    $PortableDll = Join-Path $Portable "obs-plugins/64bit/obs-multi-replay.dll"
    $PortableData = Join-Path $Portable "data/obs-plugins/obs-multi-replay"
    $Targets = @(@{ Path = $PortableDll; Source = (Join-Path $Payload "bin/64bit/obs-multi-replay.dll") },
                 @{ Path = $PortableData; Source = (Join-Path $Payload "data") })
    Set-ReplayInstallTargets $Targets
    $OldHash = (Get-FileHash -LiteralPath $PortableDll).Hash
    [IO.File]::WriteAllText((Join-Path $PortableData "old.txt"), "previous install")
    $global:ReplayFailDestination = $PortableData
    $global:ReplayFailOnce = $true
    function Move-Item {
        param([string]$LiteralPath, [string]$Destination)
        if ($global:ReplayFailOnce -and $Destination -eq $global:ReplayFailDestination -and $LiteralPath -match 'stage-') {
            $global:ReplayFailOnce = $false
            throw "Simulated disk failure during publication"
        }
        Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination
    }
    Expect-Failure { Set-ReplayInstallTargets $Targets } "Simulated disk failure"
    Remove-Item Function:\Move-Item
    Check ((Get-FileHash -LiteralPath $PortableDll).Hash -eq $OldHash) "Rollback lost the old DLL"
    Check ([IO.File]::ReadAllText((Join-Path $PortableData "old.txt")) -eq "previous install") "Rollback lost old helper data"
    $Support = Join-Path $Portable "obs-multi-replay-setup"
    [IO.Directory]::CreateDirectory($Support) | Out-Null
    [IO.File]::WriteAllText((Join-Path $Support "installation.ini"), "[Setup]`r`nMode=Portable`r`n")
    [IO.File]::WriteAllText((Join-Path $Support "unins000.exe"), "old uninstaller - never execute")
    [IO.File]::WriteAllText((Join-Path $Support "keep.txt"), "unfamiliar")
    Remove-ReplayLegacySetup "Portable" $Portable
    Check (!(Test-Path -LiteralPath (Join-Path $Support "unins000.exe"))) "Legacy uninstaller survived migration"
    Check ([IO.File]::ReadAllText((Join-Path $Support "keep.txt")) -eq "unfamiliar") "Migration deleted unfamiliar files"
    Remove-Item -LiteralPath $Support -Recurse -Force
    $Status = Get-ReplayInstallation $Portable "Portable" "0.1.0"
    Check ($Status.action -eq "Update") "Legacy install was not detected"
    $Receipt = Join-Path $PortableData "replay-installation.json"
    [IO.File]::WriteAllText($Receipt, '{"version":"0.1.0","mode":"Portable"}')
    Check ((Get-ReplayInstallation $Portable "Portable" "0.1.0").action -eq "Repair") "Same version should repair"
    Check ((Get-ReplayInstallation $Portable "Portable" "0.2.0").action -eq "Update") "New version should update"
    Expect-Failure { Get-ReplayInstallation $Portable "Portable" "0.0.9" } "Downgrading"
    # Simulate a process being terminated between two publication renames.
    $Id = [Guid]::NewGuid().ToString("N")
    $Previous = Join-Path (Split-Path -Parent $PortableDll) ".obs-multi-replay-backup-$Id"
    $Staged = Join-Path (Split-Path -Parent $PortableDll) ".obs-multi-replay-stage-$Id"
    Move-Item -LiteralPath $PortableDll -Destination $Previous
    [IO.File]::WriteAllText($PortableDll, "interrupted replacement")
    $Journal = Join-Path $Portable ".obs-multi-replay-transaction.json"
    Write-ReplayJournal $Journal @{ committed = $false; entries = @(@{ Path = $PortableDll;
        Stage = $Staged; Backup = $Previous; HadOriginal = $true; HasSource = $true }) }
    Restore-ReplayTransaction $Journal $Targets
    Check ((Get-FileHash -LiteralPath $PortableDll).Hash -eq $OldHash) "Interrupted publication did not recover"
    Check (!(Test-Path -LiteralPath $Journal)) "Recovery journal was not cleared"
    function Get-Process { param($Name, $ErrorAction) [PSCustomObject]@{ Name = "obs64" } }
    Expect-Failure { Assert-ReplayObsStopped } "Close OBS"
    Remove-Item Function:\Get-Process
    Set-ReplayInstallTargets @(@{ Path = $Installed; Source = $null }, @{ Path = $PortableDll; Source = $null },
                              @{ Path = $PortableData; Source = $null })
    Check (!(Test-Path -LiteralPath $Installed)) "Uninstall left the standard plugin"
    Check (!(Test-Path -LiteralPath $PortableDll)) "Uninstall left the portable DLL"
    Check ([IO.File]::ReadAllText($Profile) -eq "DO NOT CHANGE PROFILE") "Profile changed"
    Check ([IO.File]::ReadAllText($Other) -eq "DO NOT CHANGE OTHER PLUGIN") "Other plugin changed"
    foreach ($Path in $Preserved.Keys) { Check ((Get-FileHash -LiteralPath $Path).Hash -eq $Preserved[$Path]) "Changed user file: $Path" }
    $MissingObs = Join-Path $Temp "removed OBS"
    Set-ReplayInstallTargets @(@{ Path = (Join-Path $MissingObs "obs-plugins/64bit/obs-multi-replay.dll"); Source = $null })
    Check (!(Test-Path -LiteralPath $MissingObs)) "Uninstall recreated a removed OBS folder"
    Check (@(Get-ChildItem -LiteralPath $Temp -Recurse -Force | Where-Object { $_.Name -match '^\.obs-multi-replay-(stage|backup)-' }).Count -eq 0) "Temporary install files remain"
    Write-Host "PASS: script parsing, fixture ZIP/manifests, install/update/uninstall, portable layout, rollback, path quoting, link/OneDrive tags, corruption/x86 rejection, running OBS guard and profile preservation"
} finally {
    Remove-Item Function:\Move-Item -ErrorAction SilentlyContinue
    Remove-Item Function:\Get-Process -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Temp -Recurse -Force
}

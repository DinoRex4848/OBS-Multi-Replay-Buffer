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
    $Kit = Join-Path $Out "windows-portable-installer"
    $Payload = Join-Path $Kit "payload/obs-multi-replay"
    Assert-ReplayPackage $Kit | Out-Null
    $PackageVersion = (Assert-ReplayPackage $Kit).version
    $ZipPath = Join-Path $Out "obs-multi-replay-$PackageVersion-windows-x64-portable-installer.zip"
    Check (Test-Path -LiteralPath $ZipPath) "Installer ZIP missing"
    $Zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        # Windows PowerShell's .NET Framework can use backslashes in ZIP entry
        # names. Compare logical paths; Windows extraction accepts either form.
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "Install.cmd" }).Count -eq 1) "ZIP launcher missing"
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "payload/obs-multi-replay/data/multi-replay-writer.exe" }).Count -eq 1) "ZIP helper missing"
        Check (@($Zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq "package.json" }).Count -eq 1) "ZIP manifest missing"
    } finally { $Zip.Dispose() }
    Check (Test-Path -LiteralPath (Join-Path $Kit "Install.cmd")) "Launcher missing"
    $ManifestPath = Join-Path $Kit "package.json"
    $ManifestText = Get-Content -LiteralPath $ManifestPath -Raw
    $Installed = Join-Path $Temp "Friend's [OBS] & Files/plugins/obs-multi-replay"
    $Profile = Join-Path $Temp "profiles/basic.ini"
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Profile)) | Out-Null
    [IO.File]::WriteAllText($Profile, "DO NOT CHANGE PROFILE")
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
    function Get-Process { param($Name, $ErrorAction) [PSCustomObject]@{ Name = "obs64" } }
    Expect-Failure { Assert-ReplayObsStopped } "Close OBS"
    Remove-Item Function:\Get-Process
    Set-ReplayInstallTargets @(@{ Path = $Installed; Source = $null }, @{ Path = $PortableDll; Source = $null },
                              @{ Path = $PortableData; Source = $null })
    Check (!(Test-Path -LiteralPath $Installed)) "Uninstall left the standard plugin"
    Check (!(Test-Path -LiteralPath $PortableDll)) "Uninstall left the portable DLL"
    Check ([IO.File]::ReadAllText($Profile) -eq "DO NOT CHANGE PROFILE") "Profile changed"
    Check ([IO.File]::ReadAllText($Other) -eq "DO NOT CHANGE OTHER PLUGIN") "Other plugin changed"
    Check (@(Get-ChildItem -LiteralPath $Temp -Recurse -Force | Where-Object { $_.Name -match '^\.obs-multi-replay-(stage|backup)-' }).Count -eq 0) "Temporary install files remain"
    Write-Host "PASS: script parsing, fixture ZIP/manifests, install/update/uninstall, portable layout, rollback, path quoting, corruption/x86 rejection, running OBS guard and profile preservation"
} finally {
    Remove-Item Function:\Move-Item -ErrorAction SilentlyContinue
    Remove-Item Function:\Get-Process -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Temp -Recurse -Force
}

param(
    [string]$PackagePath = "",
    [string]$OutputDirectory = "",
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
if (!$PackagePath) { $PackagePath = Join-Path $Repo "dist/windows-x64" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $Repo "dist" }
if (!$Version) {
    $Project = Get-Content -LiteralPath (Join-Path $Repo "CMakeLists.txt") -Raw
    if ($Project -notmatch 'project\(obs-multi-replay VERSION ([0-9.]+)') { throw "Cannot read the plugin version" }
    $Version = $Matches[1]
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid package version" }
$Templates = Join-Path $Repo "packaging/windows"
. (Join-Path $Templates "Installer.Core.ps1")
$Dll = Join-Path $PackagePath "obs-plugins/64bit/obs-multi-replay.dll"
$Data = Join-Path $PackagePath "data/obs-plugins/obs-multi-replay"
$Writer = Join-Path $Data "multi-replay-writer.exe"
if (!(Test-Path -LiteralPath $Dll -PathType Leaf) -or !(Test-Path -LiteralPath $Writer -PathType Leaf) -or
    !(Test-Path -LiteralPath (Join-Path $Data "locale/en-US.ini") -PathType Leaf)) {
    throw "Compiled Windows plugin/helper/locale files are missing. Run scripts/build-windows.ps1 on Windows first."
}
Assert-ReplayWindowsBinary $Dll Dll
Assert-ReplayWindowsBinary $Writer Exe
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$Kit = Join-Path $OutputDirectory "windows-installer"
if (Test-Path -LiteralPath $Kit) { Remove-Item -LiteralPath $Kit -Recurse -Force }
$Private = Join-Path $Kit "PluginFiles"
$Payload = Join-Path $Private "payload/obs-multi-replay"
$Bin = Join-Path $Payload "bin/64bit"
[IO.Directory]::CreateDirectory($Bin) | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $Bin "obs-multi-replay.dll")
Copy-Item -LiteralPath $Data -Destination (Join-Path $Payload "data") -Recurse
Copy-Item -LiteralPath (Join-Path $Repo "LICENSE") -Destination (Join-Path $Payload "data/LICENSE")
Copy-Item -LiteralPath (Join-Path $Repo "docs/THIRD_PARTY.md") -Destination (Join-Path $Payload "data/THIRD_PARTY.md")
$Notices = Join-Path $PackagePath "third-party-notices"
if (Test-Path -LiteralPath $Notices) {
    Copy-Item -LiteralPath $Notices -Destination (Join-Path $Payload "data/third-party-notices") -Recurse
}
foreach ($Name in @("Install.cmd", "Uninstall.cmd")) {
    Copy-Item -LiteralPath (Join-Path $Templates $Name) -Destination (Join-Path $Kit $Name)
}
Copy-Item -LiteralPath (Join-Path $Templates "READ-ME-FIRST.txt") -Destination (Join-Path $Kit "README.txt")
foreach ($Name in @("Install.ps1", "Installer.Core.ps1")) {
    Copy-Item -LiteralPath (Join-Path $Templates $Name) -Destination (Join-Path $Private $Name)
}
$Root = [IO.Path]::GetFullPath($Payload).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$Files = @(Get-ChildItem -LiteralPath $Payload -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($Root.Length).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$Manifest = @{ format = 1; plugin = "obs-multi-replay"; version = $Version; platform = "windows-x64";
               minimum_obs = "32.1"; files = $Files }
$Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Private "package.json") -Encoding UTF8
Assert-ReplayPackage $Private | Out-Null
$Zip = Join-Path $OutputDirectory "obs-multi-replay-$Version-windows-x64.zip"
Write-ReplayZip $Kit $Zip
$Hash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $(Split-Path -Leaf $Zip)" | Set-Content -LiteralPath ($Zip + ".sha256") -Encoding ASCII
Write-Host "Created Windows script package: $Zip"
$NoticeText = @((Get-Content -LiteralPath (Join-Path $Payload "data/LICENSE") -Raw),
                (Get-Content -LiteralPath (Join-Path $Payload "data/THIRD_PARTY.md") -Raw))
if (Test-Path -LiteralPath $Notices) {
    foreach ($File in Get-ChildItem -LiteralPath $Notices -Recurse -File | Sort-Object FullName) {
        $NoticeText += "`r`n$($File.Name)`r`n" + (Get-Content -LiteralPath $File.FullName -Raw)
    }
}
$NoticeText -join "`r`n" | Set-Content -LiteralPath (Join-Path $OutputDirectory "windows-installer-notices.txt") -Encoding UTF8

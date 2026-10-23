param([string]$KitPath = "", [string]$OutputDirectory = "", [string]$ISCC = "")
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
if (!$KitPath) { $KitPath = Join-Path $Repo "dist/windows-installer/PluginFiles" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $Repo "dist" }
$Version = [regex]::Match((Get-Content -LiteralPath (Join-Path $Repo "CMakeLists.txt") -Raw), 'project\(obs-multi-replay VERSION ([\d.]+)').Groups[1].Value
. (Join-Path $Repo "packaging/windows/Installer.Core.ps1")
$Manifest = Assert-ReplayPackage $KitPath
if ($Manifest.version -ne $Version) { throw "Payload version differs from CMake; rebuild first." }
if (!$ISCC) {
    $Command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($Command) { $ISCC = $Command.Source }
    else { $ISCC = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6/ISCC.exe" }
}
if (!(Test-Path -LiteralPath $ISCC -PathType Leaf)) { throw "Install Inno Setup 6 (build dependency only), or specify -ISCC." }
Copy-Item -LiteralPath (Join-Path $Repo "packaging/windows/Setup.Backend.ps1") -Destination (Join-Path $KitPath "Setup.Backend.ps1")
& $ISCC "/DPluginVersion=$Version" "/DKitDir=$([IO.Path]::GetFullPath($KitPath))" `
    "/DOutputDir=$([IO.Path]::GetFullPath($OutputDirectory))" (Join-Path $Repo "packaging/windows/setup.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed: $LASTEXITCODE" }
$Setup = Join-Path $OutputDirectory "obs-multi-replay-$Version-windows-x64-setup.exe"
$Hash = (Get-FileHash -LiteralPath $Setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $(Split-Path -Leaf $Setup)" | Set-Content -LiteralPath ($Setup + ".sha256") -Encoding ASCII
Write-Host "Created offline Windows setup: $Setup"

param(
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")][string]$Configuration = "Release",
    [string]$ObsSource = "",
    [string]$Sdk = ""
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $Repo "packaging/windows/Installer.Core.ps1")
$Project = Get-Content -LiteralPath (Join-Path $Repo "CMakeLists.txt") -Raw
if ($Project -notmatch 'project\(obs-multi-replay VERSION ([0-9.]+)') { throw "Cannot read the plugin version" }
$Version = $Matches[1]
$Deps = Join-Path $Repo ".deps"
New-Item -ItemType Directory -Force $Deps | Out-Null
function Run-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (!$ObsSource) {
    $ObsSource = Join-Path $Deps "obs-studio-32.1.1"
    if (!(Test-Path (Join-Path $ObsSource "CMakeLists.txt"))) {
        Run-Native "git" @("clone", "--depth", "1", "--branch", "32.1.1", "https://github.com/obsproject/obs-studio.git", $ObsSource)
    }
}
if (!$Sdk) {
    $Sdk = Join-Path $Deps "obs-sdk-32.1.1"
    $SdkBuild = Join-Path $Deps "obs-sdk-build"
    # OBS's release-pinned presets download and checksum its Windows and Qt
    # dependencies. The UI, browser, scripting and plugin bundles aren't built.
    Run-Native "cmake" @("-S", $ObsSource, "-B", $SdkBuild, "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DENABLE_FRONTEND=OFF", "-DENABLE_PLUGINS=OFF", "-DENABLE_UI=OFF", "-DENABLE_SCRIPTING=OFF", "-DENABLE_BROWSER=OFF",
        "-DCMAKE_INSTALL_PREFIX=$Sdk")
    Run-Native "cmake" @("--build", $SdkBuild, "--config", $Configuration, "--target", "libobs", "obs-frontend-api", "--parallel")
    Run-Native "cmake" @("--install", $SdkBuild, "--config", $Configuration, "--component", "Development")
}
$ObsDeps = Join-Path $ObsSource ".deps/obs-deps-2025-08-23-x64"
$QtDeps = Join-Path $ObsSource ".deps/obs-deps-qt6-2025-08-23-x64"
if (!(Test-Path $ObsDeps) -or !(Test-Path $QtDeps)) {
    throw "OBS 32.1.1's prebuilt dependency directories are missing. Configure the release-matched OBS SDK first."
}
$Build = Join-Path $Repo "build-windows"
$Prefix = "$Sdk;$ObsDeps;$QtDeps"
Run-Native "cmake" @("-S", $Repo, "-B", $Build, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DCMAKE_PREFIX_PATH=$Prefix", "-DFFMPEG_ROOT=$ObsDeps", "-DREPLAY_INTEGRATION_TEST=OFF")
Run-Native "cmake" @("--build", $Build, "--config", $Configuration, "--parallel")
Run-Native "ctest" @("--test-dir", $Build, "-C", $Configuration, "--output-on-failure")
$Package = Join-Path $Repo "dist/windows-x64"
Run-Native "cmake" @("--install", $Build, "--config", $Configuration, "--prefix", $Package)
# The helper is a separate process and needs FFmpeg's runtime beside it. OBS
# already supplies the module's OBS/Qt/runtime dependencies in bin/64bit.
$Helper = Join-Path $Package "data/obs-plugins/obs-multi-replay"
Get-ChildItem (Join-Path $ObsDeps "bin") -Filter "*.dll" | Copy-Item -Destination $Helper
Copy-Item (Join-Path $Repo "README.md"), (Join-Path $Repo "LICENSE"), (Join-Path $Repo "CHANGELOG.md") -Destination $Package
Copy-Item (Join-Path $Repo "docs/THIRD_PARTY.md") -Destination $Package
Copy-Item (Join-Path $Repo "docs") (Join-Path $Package "docs") -Recurse -Force
$Notices = Join-Path $Package "third-party-notices"
foreach ($Dependency in @($ObsDeps, $QtDeps)) {
    Get-ChildItem $Dependency -Recurse -File | Where-Object { $_.Name -match "^(LICENSE|COPYING|NOTICE)" } | ForEach-Object {
        $Relative = $_.FullName.Substring($Dependency.Length).TrimStart([char]'\', [char]'/')
        $Target = Join-Path (Join-Path $Notices (Split-Path $Dependency -Leaf)) $Relative
        New-Item -ItemType Directory -Force (Split-Path $Target -Parent) | Out-Null
        Copy-Item $_.FullName $Target
    }
}
& (Join-Path $Repo "scripts/package-windows.ps1") -PackagePath $Package -Version $Version
Write-Host "Built Windows package. Native OBS integration acceptance tests still need to be run on Windows."

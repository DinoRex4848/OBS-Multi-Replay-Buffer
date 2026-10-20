#requires -Version 5.1
param(
    [ValidateSet("Inspect", "Install", "Uninstall", "CheckRemove")][string]$Action,
    [ValidateSet("Standard", "Portable")][string]$Mode,
    [string]$ObsRoot = "",
    [string]$PackageRoot = $PSScriptRoot,
    [string]$ResultPath
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Installer.Core.ps1")
try {
    if ($env:OS -ne "Windows_NT" -or ![Environment]::Is64BitOperatingSystem) { throw "Windows x64 is required" }
    $Message = Invoke-ReplaySetup $Action $Mode $ObsRoot $PackageRoot
    $Encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($ResultPath, $Message, $Encoding)
    @{ success = $true; message = $Message; action = $Action; mode = $Mode } | ConvertTo-Json |
        Set-Content -LiteralPath ($ResultPath + ".json") -Encoding UTF8
    exit 0
} catch {
    $Encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($ResultPath, $_.Exception.Message, $Encoding)
    @{ success = $false; message = $_.Exception.Message; action = $Action; mode = $Mode } | ConvertTo-Json |
        Set-Content -LiteralPath ($ResultPath + ".json") -Encoding UTF8
    exit 1
}

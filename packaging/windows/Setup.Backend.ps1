#requires -Version 5.1
param(
    [ValidateSet("Inspect", "Install", "Uninstall", "CheckRemove", "FindObs")][string]$Action,
    [string]$ObsRoot = "",
    [string]$PackageRoot = $PSScriptRoot,
    [Parameter(Mandatory = $true)][string]$ResultPath
)
$ErrorActionPreference = "Stop"
try {
    . (Join-Path $PSScriptRoot "Installer.Core.ps1")
    if ($env:OS -ne "Windows_NT" -or ![Environment]::Is64BitOperatingSystem) { throw "Windows x64 is required." }
    if ($Action -eq "FindObs") { $Message = Find-ReplayObs }
    elseif ($Action -eq "CheckRemove") { Assert-ReplayObsStopped; $Message = "OBS is closed." }
    else {
        if ($Action -in @("Inspect", "Install")) {
            if (!$ObsRoot) { $ObsRoot = Find-ReplayObs }
            foreach ($Marker in @("portable_mode", "portable_mode.txt", "obs_portable_mode", "obs_portable_mode.txt")) {
                if ($ObsRoot -and (Test-Path -LiteralPath (Join-Path $ObsRoot $Marker))) {
                    throw "This OBS folder is portable. Use Install.cmd in the separate Windows ZIP and choose Portable."
                }
            }
        }
        $Message = Invoke-ReplaySetup $Action "Standard" $ObsRoot $PackageRoot
    }
    [IO.File]::WriteAllText($ResultPath, [string]$Message, (New-Object Text.UTF8Encoding($false)))
    exit 0
} catch {
    $Failure = $_
    try { [IO.File]::WriteAllText($ResultPath, ($Failure.Exception.Message + "`r`n" + $Failure.ScriptStackTrace), (New-Object Text.UTF8Encoding($false))) } catch {}
    exit 1
}

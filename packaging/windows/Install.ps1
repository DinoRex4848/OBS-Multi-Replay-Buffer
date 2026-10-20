#requires -Version 5.1
param(
    [ValidateSet("Install", "Uninstall")][string]$Action = "Install",
    [ValidateSet("Standard", "Portable")][string]$Mode = "Standard",
    [string]$ObsRoot = "",
    [switch]$FromUac
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "Installer.Core.ps1")

function Select-ObsFolder {
    Add-Type -AssemblyName System.Windows.Forms
    $Dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $Dialog.Description = "Choose the OBS folder containing bin\64bit\obs64.exe"
    $Dialog.ShowNewFolderButton = $false
    try {
        if ($Dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw "Installation cancelled." }
        return $Dialog.SelectedPath
    } finally { $Dialog.Dispose() }
}

function Find-StandardObs {
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

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or ![Environment]::Is64BitOperatingSystem) {
        throw "This installer requires 64-bit Windows and 64-bit OBS Studio."
    }
    Write-Host "OBS Multi-Length Replay - $Action"
    Assert-ReplayObsStopped
    if ($Action -eq "Install") { $Package = Assert-ReplayPackage $PSScriptRoot }
    if (!$ObsRoot) {
        if ($Mode -eq "Portable") { $ObsRoot = Select-ObsFolder }
        else { $ObsRoot = Find-StandardObs }
    }
    if ($Action -eq "Install" -and !$ObsRoot) { $ObsRoot = Select-ObsFolder }
    if ($ObsRoot) {
        $ObsRoot = [IO.Path]::GetFullPath($ObsRoot)
        $ObsExe = Join-Path $ObsRoot "bin/64bit/obs64.exe"
        if (!(Test-Path -LiteralPath $ObsExe -PathType Leaf)) { throw "OBS was not found in the selected folder." }
        if ($Action -eq "Install") {
            Assert-ReplayWindowsBinary $ObsExe Exe
            $Version = [Diagnostics.FileVersionInfo]::GetVersionInfo($ObsExe).ProductVersion
            if ($Version -notmatch '(\d+\.\d+(?:\.\d+){0,2})' -or [version]$Matches[1] -lt [version]$Package.minimum_obs) {
                throw "Install OBS Studio $($Package.minimum_obs) or newer first."
            }
        }
    }
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = New-Object Security.Principal.WindowsPrincipal($Identity)
    if ($Mode -eq "Standard" -and !$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        # Pass paths as encoded JSON, so spaces, apostrophes and shell characters
        # survive the UAC relaunch without being interpreted as commands.
        $Arguments = @{ Script = $PSCommandPath; Action = $Action; Mode = $Mode; ObsRoot = $ObsRoot } | ConvertTo-Json -Compress
        $Data = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Arguments))
        $Command = '$a = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("' + $Data + '")) | ConvertFrom-Json; ' +
                   '& $a.Script -Action $a.Action -Mode $a.Mode -ObsRoot $a.ObsRoot -FromUac; exit $LASTEXITCODE'
        $Encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
        $Process = Start-Process -FilePath (Join-Path $PSHOME "powershell.exe") -Verb RunAs -Wait -PassThru `
                                -ArgumentList "-NoProfile -ExecutionPolicy Bypass -EncodedCommand $Encoded"
        if ($Process.ExitCode -eq 0) {
            if ($Action -eq "Install") {
                Write-Host "Installed. Restart OBS and open Docks > Multi-Length Replay and Multi-Buffer Settings."
            } else { Write-Host "Plugin removed. Profiles, hotkeys and recordings are preserved." }
        } else { Write-Host "Installer did not finish. The detailed error was shown in the administrator window." }
        exit $Process.ExitCode
    }
    Assert-ReplayObsStopped
    $Message = Invoke-ReplaySetup $Action $Mode $ObsRoot $PSScriptRoot
    $Installing = $Action -eq "Install"
    Write-Host $Message
    if ($Installing) {
        Write-Host "Installed version $($Package.version) "
        Write-Host "Restart OBS, then open Docks > Multi-Length Replay and Multi-Buffer Settings."
        Write-Host "Assign replay shortcuts in OBS Settings > Hotkeys."
    } else {
        Write-Host "Plugin removed. Your OBS profiles, hotkeys and saved recordings are preserved."
    }
    exit 0
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($Mode -eq "Portable") { Write-Host "If the OBS folder is protected, right-click the launcher and run as administrator." }
    if ($FromUac) { Read-Host "Press Enter to close this error window" | Out-Null }
    exit 1
}

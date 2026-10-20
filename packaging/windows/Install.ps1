#requires -Version 5.1
param(
    [ValidateSet("Install", "Uninstall")][string]$Action = "Install",
    [ValidateSet("Standard", "Portable", "")][string]$Mode = "",
    [string]$ObsRoot = "",
    [switch]$NonInteractive,
    [switch]$FromUac,
    [string]$LogPath = ""
)
$ErrorActionPreference = "Stop"
$ExitCode = 1
function Write-ReplayMessage([string]$Message) {
    Write-Host $Message
    if ($LogPath) { [IO.File]::AppendAllText($LogPath, $Message + [Environment]::NewLine) }
}
try {
    . (Join-Path $PSScriptRoot "Installer.Core.ps1")
    if ($env:OS -ne "Windows_NT" -or ![Environment]::Is64BitOperatingSystem) { throw "Windows x64 is required." }
    $LogDirectory = Join-Path $PSScriptRoot "Logs"
    Assert-ReplaySafePath $LogDirectory
    [IO.Directory]::CreateDirectory($LogDirectory) | Out-Null
    if (!$LogPath) { $LogPath = Join-Path $LogDirectory ("$Action-" + [Guid]::NewGuid().ToString("N") + ".log") }
    # Only the elevated child reuses a log, and it must stay inside this package.
    if ((Split-Path -Parent ([IO.Path]::GetFullPath($LogPath))) -ne [IO.Path]::GetFullPath($LogDirectory)) { throw "Invalid log location" }
    Assert-ReplaySafePath $LogPath
    Write-ReplayMessage "OBS Multi-Length Replay - $Action"
    Write-ReplayMessage "Log: $LogPath"
    Assert-ReplayObsStopped
    if (!$Mode) {
        if ($NonInteractive) { throw "Specify -Mode Standard or -Mode Portable in noninteractive mode." }
        $Choice = Read-Host "OBS installation: [1] Normal (default), [2] Portable, [Q] Cancel"
        if ($Choice -match '^(q|Q)$') { throw [OperationCanceledException]::new("Cancelled. No plugin files were changed.") }
        if ($Choice -notin @("", "1", "2")) { throw "Choose 1 or 2." }
        $Mode = if ($Choice -eq "2") { "Portable" } else { "Standard" }
    }
    if (!$ObsRoot -and $Mode -eq "Standard") { $ObsRoot = Find-ReplayObs }
    if (!$ObsRoot -and ($Mode -eq "Portable" -or $Action -eq "Install")) {
        if ($NonInteractive) { throw "OBS was not found. Specify -ObsRoot with the OBS root folder." }
        $ObsRoot = (Read-Host "OBS root folder (containing bin\64bit\obs64.exe); leave empty to cancel").Trim().Trim('"')
        if (!$ObsRoot) { throw [OperationCanceledException]::new("Cancelled. No plugin files were changed.") }
    }
    if ($ObsRoot) { $ObsRoot = [IO.Path]::GetFullPath($ObsRoot); Assert-ReplaySafePath $ObsRoot }
    if ($Action -eq "Install") {
        Write-ReplayMessage (Invoke-ReplaySetup "Inspect" $Mode $ObsRoot $PSScriptRoot)
    } else {
        $Destination = if ($Mode -eq "Standard") { Join-Path $env:ProgramData "obs-studio/plugins/obs-multi-replay" } else { $ObsRoot }
        Write-ReplayMessage "Remove only Multi-Length Replay from: $Destination"
    }
    if (!$NonInteractive -and !$FromUac -and (Read-Host "Continue? [y/N]") -notmatch '^(y|yes)$') {
        throw [OperationCanceledException]::new("Cancelled. No plugin files were changed.")
    }
    $Principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    $Admin = $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $NeedsAdmin = $Mode -eq "Standard"
    if ($Mode -eq "Portable") {
        foreach ($Path in @($ObsRoot, (Join-Path $ObsRoot "obs-plugins/64bit"), (Join-Path $ObsRoot "data/obs-plugins"), (Join-Path $ObsRoot "obs-multi-replay-setup"))) {
            if (!(Test-ReplayWritablePath $Path)) { $NeedsAdmin = $true }
        }
    }
    if ($NeedsAdmin -and !$Admin) {
        if ($NonInteractive -or $FromUac) { throw "Administrator access is required for this target. Run the launcher as administrator or use interactive mode." }
        $Arguments = @{ Script = $PSCommandPath; Action = $Action; Mode = $Mode; ObsRoot = $ObsRoot; LogPath = $LogPath } | ConvertTo-Json -Compress
        $Data = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Arguments))
        $Command = '$a = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("' + $Data + '")) | ConvertFrom-Json; ' +
                   '& $a.Script -Action $a.Action -Mode $a.Mode -ObsRoot $a.ObsRoot -LogPath $a.LogPath -FromUac; exit $LASTEXITCODE'
        $Encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
        $Process = Start-Process -FilePath (Join-Path $PSHOME "powershell.exe") -Verb RunAs -Wait -PassThru `
                                -ArgumentList "-NoProfile -ExecutionPolicy Bypass -EncodedCommand $Encoded"
        $ExitCode = $Process.ExitCode
        if ($ExitCode -ne 0) { Write-ReplayMessage "The elevated installer failed. See the error details in $LogPath" }
    } else {
        Invoke-ReplaySetup $Action $Mode $ObsRoot $PSScriptRoot 3>&1 | ForEach-Object { Write-ReplayMessage $_.ToString() }
        if ($Action -eq "Install") { Write-ReplayMessage "Restart OBS. Open Docks > Multi-Length Replay and Tools > Multi-Buffer Settings." }
        $ExitCode = 0
    }
} catch [OperationCanceledException] {
    Write-ReplayMessage $_.Exception.Message
    $ExitCode = 0
} catch {
    $Failure = $_
    # Logging failures must not hide the original installation error.
    try { Write-ReplayMessage ("ERROR: " + $Failure.Exception.Message) }
    catch { Write-Host ("ERROR: " + $Failure.Exception.Message) }
    if ($LogPath -and (Test-Path -LiteralPath $LogPath)) {
        try { [IO.File]::AppendAllText($LogPath, $Failure.ScriptStackTrace + [Environment]::NewLine) } catch {}
        Write-Host "Log: $LogPath"
    }
} finally {
    if (!$NonInteractive -and !$FromUac) { Read-Host "Press Enter to close" | Out-Null }
}
exit $ExitCode

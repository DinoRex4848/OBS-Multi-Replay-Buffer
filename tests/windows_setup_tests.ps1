# Native acceptance checks for the real compiled EXE, never fixture payloads.
# Run on a disposable Windows builder; the standard-install test uses ProgramData.
param([string]$SetupPath = "", [switch]$SmokeObs)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Version = [regex]::Match((Get-Content (Join-Path $Repo "CMakeLists.txt") -Raw), 'project\(obs-multi-replay VERSION ([\d.]+)').Groups[1].Value
if (!$SetupPath) { $SetupPath = Join-Path $Repo "dist/obs-multi-replay-$Version-windows-x64-setup.exe" }
$SetupPath = [IO.Path]::GetFullPath($SetupPath)
$Scratch = Join-Path $env:TEMP ("Replay setup's [acceptance] " + [Guid]::NewGuid().ToString("N"))
$Standard = Join-Path $env:ProgramData "obs-studio/plugins/obs-multi-replay"
$DiscoveryKey = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio"
$DiscoveryKeyCreated = $false
if (Test-Path -LiteralPath $Standard) { throw "Use a disposable builder without an existing replay plugin" }
function Check([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
$script:SetupAttempt = 0
$Logs = Join-Path $Repo "build-windows-setup-logs"
[IO.Directory]::CreateDirectory($Logs) | Out-Null
function Wait-Installer([Diagnostics.Process]$Process, [string]$Log) {
    if (!$Process.WaitForExit(120000)) {
        $Process.Kill()
        if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Tail 60 | Write-Host }
        throw "Installer timed out; see $Log"
    }
    if ($Process.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Tail 60 | Write-Host }
        throw "Installer failed with exit code $($Process.ExitCode); see $Log"
    }
}
function Run-Setup([string]$Mode, [string]$Root) {
    $Arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /MODE=' + $Mode
    if ($Root) { $Arguments += ' /OBSROOT="' + $Root + '"' }
    if ($Mode -eq "Standard") { $Arguments += ' /ALLUSERS' }
    $script:SetupAttempt++
    $Log = Join-Path $Logs "setup-$script:SetupAttempt.log"
    $Arguments += ' /LOG="' + $Log + '"'
    $Process = Start-Process $SetupPath -ArgumentList $Arguments -PassThru
    Wait-Installer $Process $Log
}
function Run-Remove([string]$Uninstaller) {
    $script:SetupAttempt++
    $Log = Join-Path $Logs "remove-$script:SetupAttempt.log"
    $Process = Start-Process $Uninstaller -ArgumentList ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + $Log + '"') -PassThru
    Wait-Installer $Process $Log
}
try {
    New-Item -ItemType Directory -Path $Scratch | Out-Null
    $Zip = Join-Path $Scratch "obs.zip"
    # WebClient uses a literal filename; Windows PowerShell OutFile treats brackets as wildcards.
    $Client = New-Object Net.WebClient
    try { $Client.DownloadFile("https://github.com/obsproject/obs-studio/releases/download/32.1.1/OBS-Studio-32.1.1-Windows-x64.zip", $Zip) }
    finally { $Client.Dispose() }
    Check ((Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash -eq "a4cc40a06d5a5dc158c792631b596a04b02d8fe99644a0f0bc3b12199dbce011") "OBS runtime checksum mismatch"
    $Obs = Join-Path $Scratch "portable OBS"
    Expand-Archive -LiteralPath $Zip -DestinationPath $Obs
    [IO.File]::WriteAllText((Join-Path $Obs "portable_mode.txt"), "")
    $Profile = Join-Path $Obs "config/obs-studio/basic/profiles/Test/basic.ini"
    New-Item -ItemType Directory -Path (Split-Path -Parent $Profile) -Force | Out-Null
    [IO.File]::WriteAllText($Profile, "[MultiLengthReplay]`nSettings={}`n")
    $ProfileHash = (Get-FileHash -LiteralPath $Profile).Hash
    Run-Setup "Portable" $Obs
    $Dll = Join-Path $Obs "obs-plugins/64bit/obs-multi-replay.dll"
    Check (Test-Path -LiteralPath $Dll) "Portable DLL missing"
    $Data = Join-Path $Obs "data/obs-plugins/obs-multi-replay"
    [IO.File]::WriteAllText((Join-Path $Data "stale-dependency.dll"), "stale")
    Run-Setup "Portable" $Obs
    Check (!(Test-Path -LiteralPath (Join-Path $Data "stale-dependency.dll"))) "Repair retained stale dependencies"
    Check ((Get-FileHash -LiteralPath $Profile).Hash -eq $ProfileHash) "Repair changed settings"
    # Resolve the actual plugin's imports using the runtime OBS directory.
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ReplayDllProbe {
 [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibrary(string path);
 [DllImport("kernel32", CharSet=CharSet.Unicode)] public static extern bool SetDllDirectory(string path);
 [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr handle);
}
'@
    [ReplayDllProbe]::SetDllDirectory((Join-Path $Obs "bin/64bit")) | Out-Null
    $Handle = [ReplayDllProbe]::LoadLibrary($Dll)
    Check ($Handle -ne [IntPtr]::Zero) "The production plugin's DLL imports did not resolve"
    [ReplayDllProbe]::FreeLibrary($Handle) | Out-Null
    [ReplayDllProbe]::SetDllDirectory($null) | Out-Null
    # Launch the installed writer with EOF input to prove its bundled imports resolve.
    $WriterInfo = New-Object Diagnostics.ProcessStartInfo
    $WriterInfo.FileName = Join-Path $Data "multi-replay-writer.exe"
    $WriterInfo.WorkingDirectory = $Data
    $WriterInfo.UseShellExecute = $false
    $WriterInfo.CreateNoWindow = $true
    $WriterInfo.RedirectStandardInput = $true
    $WriterInfo.RedirectStandardError = $true
    $WriterInfo.EnvironmentVariables["PATH"] = Join-Path $env:WINDIR "System32"
    $Writer = [Diagnostics.Process]::Start($WriterInfo)
    try {
        $Writer.StandardInput.Close()
        Check ($Writer.WaitForExit(15000)) "Installed writer did not exit after EOF"
        $WriterError = $Writer.StandardError.ReadToEnd()
        Check ($Writer.ExitCode -eq 1 -and $WriterError.Contains("Truncated writer input")) "Installed writer's dependencies did not resolve: $WriterError"
    } finally {
        if (!$Writer.HasExited) { $Writer.Kill(); $Writer.WaitForExit() }
        $Writer.Dispose()
    }
    if ($SmokeObs) {
        $Process = Start-Process (Join-Path $Obs "bin/64bit/obs64.exe") -WorkingDirectory (Join-Path $Obs "bin/64bit") `
            -ArgumentList '--portable --multi --disable-updater --disable-shutdown-check --disable-missing-files-check' -PassThru
        try {
            Start-Sleep -Seconds 10
            $Logs = Get-ChildItem (Join-Path $Obs "config/obs-studio/logs") -Filter *.txt | Sort-Object LastWriteTime
            Check ($Logs.Count -gt 0) "OBS did not write a startup log"
            $Log = Get-Content $Logs[-1].FullName -Raw
            Check ($Log -match '\[multi-replay\]') "Plugin did not initialize in OBS"
            Check ($Process.CloseMainWindow()) "Could not close isolated OBS normally"
            Check ($Process.WaitForExit(15000)) "OBS did not close normally"
        } finally { if (!$Process.HasExited) { $Process.Kill(); $Process.WaitForExit() } }
    }
    $ProfileHash = (Get-FileHash -LiteralPath $Profile).Hash
    $Moved = Join-Path $Scratch "moved portable OBS"
    Move-Item -LiteralPath $Obs -Destination $Moved
    Run-Remove (Join-Path $Moved "obs-multi-replay-setup/unins000.exe")
    Check (!(Test-Path -LiteralPath (Join-Path $Moved "obs-plugins/64bit/obs-multi-replay.dll"))) "Moved portable uninstall left DLL"
    Check ((Get-FileHash -LiteralPath (Join-Path $Moved "config/obs-studio/basic/profiles/Test/basic.ini")).Hash -eq $ProfileHash) "Uninstall changed settings"
    if (!(Test-Path -LiteralPath $DiscoveryKey)) {
        New-Item -Path $DiscoveryKey -Force | Out-Null
        $DiscoveryKeyCreated = $true
        New-ItemProperty -Path $DiscoveryKey -Name InstallLocation -Value $Moved -PropertyType String | Out-Null
        Run-Setup "Standard" ""  # Exercise registry discovery, without /OBSROOT.
    } else {
        Run-Setup "Standard" $Moved
    }
    Check (Test-Path -LiteralPath (Join-Path $Standard "bin/64bit/obs-multi-replay.dll")) "Standard installation missing"
    Run-Setup "Standard" $Moved
    $Key = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS-Multi-Length-Replay_is1"
    $Registration = Get-ItemProperty -LiteralPath $Key
    Check ($Registration.DisplayVersion -eq $Version) "Installed Apps version is incorrect"
    Run-Remove $Registration.UninstallString.Trim('"')
    Check (!(Test-Path -LiteralPath $Standard)) "Standard uninstall left plugin"
    Check (!(Test-Path -LiteralPath $Key)) "Standard uninstall left Installed Apps registration"
    Write-Host "PASS: compiled EXE install/repair, dependency loading, portable relocation/removal, standard registration/removal, settings preservation"
} finally {
    if ($DiscoveryKeyCreated) { Remove-Item -LiteralPath $DiscoveryKey -Force }
    # Do not silently erase a failed installation: keep it for inspection.
    if (!(Test-Path -LiteralPath $Standard)) { Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue }
}

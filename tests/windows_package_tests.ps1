# Native acceptance checks for the extracted production script package, never fixture payloads.
# Run on a disposable Windows builder; the standard-install test uses ProgramData.
param([string]$PackagePath = "", [string]$SetupPath = "", [switch]$SmokeObs)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Version = [regex]::Match((Get-Content (Join-Path $Repo "CMakeLists.txt") -Raw), 'project\(obs-multi-replay VERSION ([\d.]+)').Groups[1].Value
if (!$PackagePath) { $PackagePath = Join-Path $Repo "dist/obs-multi-replay-$Version-windows-x64.zip" }
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
if (!$SetupPath) { $SetupPath = Join-Path $Repo "dist/obs-multi-replay-$Version-windows-x64-setup.exe" }
$Scratch = Join-Path $env:TEMP ("Replay setup's [acceptance] " + [Guid]::NewGuid().ToString("N"))
$Standard = Join-Path $env:ProgramData "obs-studio/plugins/obs-multi-replay"
$DiscoveryKey = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio"
$DiscoveryKeyCreated = $false
if (Test-Path -LiteralPath $Standard) { throw "Use a disposable builder without an existing replay plugin" }
function Check([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
$Logs = Join-Path $Repo "build-windows-package-logs"
$Package = Join-Path $Scratch "Friend's [extracted] & package"
$LegacySupport = Join-Path $env:ProgramFiles "OBS Multi-Length Replay"
$Key = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS-Multi-Length-Replay_is1"
if ((Test-Path -LiteralPath $Key) -or (Test-Path -LiteralPath $LegacySupport)) { throw "Use a disposable builder without legacy replay setup" }
$LegacyCreated = $false
$SetupSupport = Join-Path $env:ProgramFiles "OBS Multi Replay Buffer"
$SetupKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS-Multi-Replay-Buffer_is1"
$AppDiscoveryKey = "HKCU:\SOFTWARE\OBS Studio"
$AppDiscoveryCreated = $false
if ((Test-Path -LiteralPath $SetupKey) -or (Test-Path -LiteralPath $SetupSupport)) { throw "Use a disposable builder without an existing setup installation" }
function Run-Package([string]$Action, [string]$Mode, [string]$Root, [int]$Expected = 0, [switch]$Cancel) {
    $Launcher = Join-Path $Package "$Action.cmd"
    $Command = '""' + $Launcher + '" -Mode ' + $Mode
    if ($Root) { $Command += ' -ObsRoot "' + $Root + '"' }
    if (!$Cancel) { $Command += ' -NonInteractive' }
    $Command += '"'
    $Info = New-Object Diagnostics.ProcessStartInfo
    $Info.FileName = $env:ComSpec
    $Info.Arguments = '/d /s /c ' + $Command
    $Info.UseShellExecute = $false
    $Info.RedirectStandardInput = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::Start($Info)
    $Output = $Process.StandardOutput.ReadToEndAsync()
    $ErrorOutput = $Process.StandardError.ReadToEndAsync()
    if ($Cancel) { $Process.StandardInput.WriteLine("n"); $Process.StandardInput.WriteLine("") }
    $Process.StandardInput.Close()
    if (!$Process.WaitForExit(120000)) { $Process.Kill(); throw "Script package timed out" }
    Write-Host $Output.Result
    Write-Host $ErrorOutput.Result
    Check ($Process.ExitCode -eq $Expected) "$Action failed: expected $Expected, got $($Process.ExitCode)"
    $Process.Dispose()
}
function Run-Setup([string]$Executable, [string]$Arguments, [int]$Expected = 0) {
    $Process = Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru
    try {
        if (!$Process.WaitForExit(120000)) { $Process.Kill(); throw "Setup timed out: $Arguments" }
        Check ($Process.ExitCode -eq $Expected) "Setup failed: expected $Expected, got $($Process.ExitCode). See setup logs."
    } finally { $Process.Dispose() }
}
function Run-InteractiveSetup([string]$Arguments) {
    # Exercise the ordinary wizard, with no /SILENT or /ALLUSERS bypass.
    # Hosted builders are already administrators; interactive UAC remains manual.
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $Process = Start-Process -FilePath $SetupPath -ArgumentList $Arguments -PassThru
    $Deadline = [DateTime]::UtcNow.AddSeconds(120)
    try {
        while (!$Process.HasExited -and [DateTime]::UtcNow -lt $Deadline) {
            Start-Sleep -Milliseconds 300
            $Process.Refresh()
            # Inno may launch its extracted child process, so locate by window name.
            $Windows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
                [Windows.Automation.TreeScope]::Children,
                (New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, "Setup - OBS Multi-Length Replay")))
            foreach ($Window in $Windows) {
                $Buttons = $Window.FindAll([Windows.Automation.TreeScope]::Descendants,
                    (New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)))
                foreach ($Button in $Buttons) {
                    if ($Button.Current.IsEnabled -and $Button.Current.Name.Replace('&', '') -match '^(Next >|Install|Finish)$') {
                        $Pattern = $Button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
                        $Pattern.Invoke()
                        break
                    }
                }
                # The license acceptance radio button precedes the disabled Next button.
                $Accept = $Window.FindFirst([Windows.Automation.TreeScope]::Descendants,
                    (New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, "I accept the agreement")))
                if ($Accept) { $Accept.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select() }
            }
        }
        Check $Process.HasExited "Interactive setup timed out; inspect its log and desktop session."
        Check ($Process.ExitCode -eq 0) "Interactive setup failed: $($Process.ExitCode)"
    } finally {
        if (!$Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
        $Process.Dispose()
    }
}
try {
    New-Item -ItemType Directory -Path $Scratch | Out-Null
    Expand-Archive -LiteralPath $PackagePath -DestinationPath $Package
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
    Run-Package "Install" "Portable" $Obs
    $Dll = Join-Path $Obs "obs-plugins/64bit/obs-multi-replay.dll"
    Check (Test-Path -LiteralPath $Dll) "Portable DLL missing"
    $Data = Join-Path $Obs "data/obs-plugins/obs-multi-replay"
    [IO.File]::WriteAllText((Join-Path $Data "stale-dependency.dll"), "stale")
    $LegacyPortable = Join-Path $Obs "obs-multi-replay-setup"
    [IO.Directory]::CreateDirectory($LegacyPortable) | Out-Null
    [IO.File]::WriteAllText((Join-Path $LegacyPortable "installation.ini"), "[Setup]`r`nMode=Portable`r`n")
    [IO.File]::WriteAllText((Join-Path $LegacyPortable "unins000.exe"), "obsolete fixture - never execute")
    [IO.File]::WriteAllText((Join-Path $Data "replay-installation.json"), '{"version":"0.1.0","mode":"Portable"}')
    Run-Package "Install" "Portable" $Obs
    Check (!(Test-Path -LiteralPath $LegacyPortable)) "Portable EXE support survived upgrade"
    Run-Package "Install" "Portable" $Obs
    Run-Package "Install" "Portable" $Obs -Cancel
    Check (!(Test-Path -LiteralPath (Join-Path $Data "stale-dependency.dll"))) "Repair retained stale dependencies"
    Check ((Get-FileHash -LiteralPath $Profile).Hash -eq $ProfileHash) "Repair changed settings"
    $InstalledHash = (Get-FileHash -LiteralPath $Dll).Hash
    $PayloadWriter = Join-Path $Package "PluginFiles/payload/obs-multi-replay/data/multi-replay-writer.exe"
    $WriterBytes = [IO.File]::ReadAllBytes($PayloadWriter)
    [IO.File]::AppendAllText($PayloadWriter, "damaged")
    Run-Package "Install" "Portable" $Obs 1
    Check ((Get-FileHash -LiteralPath $Dll).Hash -eq $InstalledHash) "Damaged payload changed installation"
    [IO.File]::WriteAllBytes($PayloadWriter, $WriterBytes)
    $Mutex = New-Object Threading.Mutex($true, "Global\OBS-Multi-Length-Replay-Installer")
    try { Run-Package "Install" "Portable" $Obs 1 }
    finally { $Mutex.ReleaseMutex(); $Mutex.Dispose() }
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
        $Config = Join-Path $Obs "config/obs-studio"
        $Initial = "[General]`nFirstRun=true`n[Basic]`nProfile=Test`nProfileDir=Test`nSceneCollection=Test`nSceneCollectionFile=Test`n"
        [IO.File]::WriteAllText((Join-Path $Config "global.ini"), $Initial)
        [IO.File]::WriteAllText((Join-Path $Config "user.ini"), $Initial + "[BasicWindow]`nPreviewEnabled=false`n")
        # Start-Process treats WorkingDirectory as a wildcard path in PowerShell
        # 5.1. ProcessStartInfo preserves the literal apostrophes/brackets tested here.
        $ObsInfo = New-Object Diagnostics.ProcessStartInfo
        $ObsInfo.FileName = Join-Path $Obs "bin/64bit/obs64.exe"
        $ObsInfo.WorkingDirectory = Join-Path $Obs "bin/64bit"
        $ObsInfo.Arguments = '--portable --multi --disable-updater --disable-shutdown-check --disable-missing-files-check'
        $ObsInfo.UseShellExecute = $false
        $Process = [Diagnostics.Process]::Start($ObsInfo)
        try {
            Start-Sleep -Seconds 10
            $RuntimeLogs = @(Get-ChildItem -LiteralPath (Join-Path $Obs "config/obs-studio/logs") -Filter *.txt | Sort-Object LastWriteTime)
            Check ($RuntimeLogs.Count -gt 0) "OBS did not write a startup log"
            $Log = Get-Content -LiteralPath $RuntimeLogs[-1].FullName -Raw
            Check ($Log -match '\[multi-replay\] Replay dock and Tools settings ready; manual startup') "Plugin did not finish initializing in OBS"
            Check ($Log -notmatch '\[multi-replay\] Session') "Replay unexpectedly started during OBS startup"
            Check ($Process.CloseMainWindow()) "Could not close isolated OBS normally"
            Check ($Process.WaitForExit(15000)) "OBS did not close normally"
            Check ($Process.ExitCode -eq 0) "OBS exited with an error"
            [IO.Directory]::CreateDirectory($Logs) | Out-Null
            Copy-Item -LiteralPath $RuntimeLogs[-1].FullName -Destination (Join-Path $Logs "obs-startup-shutdown.log")
        } finally {
            if (!$Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
            $Process.Dispose()
        }
    }
    $ProfileHash = (Get-FileHash -LiteralPath $Profile).Hash
    $Moved = Join-Path $Scratch "moved portable OBS"
    Move-Item -LiteralPath $Obs -Destination $Moved
    Run-Package "Uninstall" "Portable" $Moved
    Run-Package "Uninstall" "Portable" $Moved
    Check (!(Test-Path -LiteralPath (Join-Path $Moved "obs-plugins/64bit/obs-multi-replay.dll"))) "Moved portable uninstall left DLL"
    Check ((Get-FileHash -LiteralPath (Join-Path $Moved "config/obs-studio/basic/profiles/Test/basic.ini")).Hash -eq $ProfileHash) "Uninstall changed settings"
    if (!(Test-Path -LiteralPath $DiscoveryKey)) {
        New-Item -Path $DiscoveryKey -Force | Out-Null
        $DiscoveryKeyCreated = $true
        New-ItemProperty -Path $DiscoveryKey -Name InstallLocation -Value $Moved -PropertyType String | Out-Null
        Run-Package "Install" "Standard" ""  # Exercise registry discovery, without /OBSROOT.
    } else {
        Run-Package "Install" "Standard" $Moved
    }
    Check (Test-Path -LiteralPath (Join-Path $Standard "bin/64bit/obs-multi-replay.dll")) "Standard installation missing"
    Run-Package "Install" "Standard" $Moved
    Check (!(Test-Path -LiteralPath $Key)) "Script installation created Installed Apps registration"
    # A recognizable legacy EXE registration must be retired without invoking it.
    [IO.Directory]::CreateDirectory($LegacySupport) | Out-Null
    [IO.File]::WriteAllText((Join-Path $LegacySupport "installation.ini"), "[Setup]`r`nMode=Standard`r`n")
    [IO.File]::WriteAllText((Join-Path $LegacySupport "unins000.exe"), "obsolete fixture - never execute")
    New-Item -Path $Key -Force | Out-Null
    $LegacyCreated = $true
    New-ItemProperty -Path $Key -Name DisplayName -Value "OBS Multi-Length Replay" -PropertyType String | Out-Null
    New-ItemProperty -Path $Key -Name UninstallString -Value ('"' + (Join-Path $LegacySupport "unins000.exe") + '"') -PropertyType String | Out-Null
    Run-Package "Install" "Standard" $Moved
    Check (!(Test-Path -LiteralPath $Key)) "Legacy Installed Apps entry survived update"
    Check (!(Test-Path -LiteralPath $LegacySupport)) "Legacy setup support survived update"
    Move-Item -LiteralPath (Join-Path $Package "PluginFiles/payload") -Destination (Join-Path $Package "PluginFiles/payload-unavailable")
    $ObsExeBytes = [IO.File]::ReadAllBytes((Join-Path $Moved "bin/64bit/obs64.exe"))
    Remove-Item -LiteralPath (Join-Path $Moved "bin/64bit/obs64.exe")
    Run-Package "Uninstall" "Standard" ""
    Run-Package "Uninstall" "Standard" ""
    Check (!(Test-Path -LiteralPath $Standard)) "Standard uninstall left plugin"
    Check ((Get-FileHash -LiteralPath (Join-Path $Moved "config/obs-studio/basic/profiles/Test/basic.ini")).Hash -eq $ProfileHash) "Standard removal changed settings"
    # A conventional EXE is the default for normal OBS. No portable marker here.
    [IO.File]::WriteAllBytes((Join-Path $Moved "bin/64bit/obs64.exe"), $ObsExeBytes)
    [IO.Directory]::CreateDirectory($Logs) | Out-Null
    Run-Setup $SetupPath ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OBSROOT="' + $Moved + '" /LOG="' + (Join-Path $Logs "exe-portable-refused.log") + '"') 5
    Check (!(Test-Path -LiteralPath $Standard)) "Normal setup changed portable OBS"
    Remove-Item -LiteralPath (Join-Path $Moved "portable_mode.txt")
    if (!(Test-Path -LiteralPath $AppDiscoveryKey)) {
        New-Item -Path $AppDiscoveryKey -Force | Out-Null
        $AppDiscoveryCreated = $true
        Set-Item -LiteralPath $AppDiscoveryKey -Value $Moved
        # No explicit OBSROOT: exercise the application registry's default value.
        Run-InteractiveSetup ('/NORESTART /LOG="' + (Join-Path $Logs "exe-interactive.log") + '"')
    } else {
        Run-InteractiveSetup ('/NORESTART /OBSROOT="' + $Moved + '" /LOG="' + (Join-Path $Logs "exe-interactive.log") + '"')
    }
    Check (Test-Path -LiteralPath (Join-Path $Standard "bin/64bit/obs-multi-replay.dll")) "Interactive EXE installation missing"
    Check (Test-Path -LiteralPath $SetupKey) "Installed Apps registration missing"
    Run-Setup $SetupPath ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OBSROOT="' + $Moved + '" /LOG="' + (Join-Path $Logs "exe-repair.log") + '"')
    Remove-Item -LiteralPath (Join-Path $Moved "bin/64bit/obs64.exe")
    Run-Setup (Join-Path $SetupSupport "unins000.exe") ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + (Join-Path $Logs "exe-uninstall.log") + '"')
    Check (!(Test-Path -LiteralPath $Standard)) "EXE removal left plugin files"
    Check (!(Test-Path -LiteralPath $SetupKey)) "EXE removal left Installed Apps registration"
    Check ((Get-FileHash -LiteralPath (Join-Path $Moved "config/obs-studio/basic/profiles/Test/basic.ini")).Hash -eq $ProfileHash) "EXE changed settings"
    Write-Host "PASS: extracted CMD transactions/dependencies, ordinary EXE wizard, EXE repair/registration/removal, portable relocation and settings preservation"
} finally {
    $PackageLogs = Join-Path $Package "PluginFiles/Logs"
    if (Test-Path -LiteralPath $PackageLogs) {
        [IO.Directory]::CreateDirectory($Logs) | Out-Null
        Get-ChildItem -LiteralPath $PackageLogs -File | Copy-Item -Destination $Logs
    }
    if ($LegacyCreated -and (Test-Path -LiteralPath $Key)) { Remove-Item -LiteralPath $Key -Force }
    if ($LegacyCreated -and (Test-Path -LiteralPath $LegacySupport)) { Remove-Item -LiteralPath $LegacySupport -Recurse -Force }
    if ($DiscoveryKeyCreated) { Remove-Item -LiteralPath $DiscoveryKey -Force }
    if ($AppDiscoveryCreated) { Remove-Item -LiteralPath $AppDiscoveryKey -Force }
    # Do not silently erase a failed installation: keep it for inspection.
    if (!(Test-Path -LiteralPath $Standard)) { Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue }
}

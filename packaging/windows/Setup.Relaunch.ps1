param([string]$Installer, [string]$ObsRoot, [string]$Mode, [switch]$Silent)
$ErrorActionPreference = "Stop"
try {
    $Arguments = '/ALLUSERS /MODE=' + $Mode + ' /OBSROOT="' + $ObsRoot.TrimEnd('\') + '"'
    if ($Silent) { $Arguments += ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART' }
    $Process = Start-Process -FilePath $Installer -Verb RunAs -ArgumentList $Arguments -Wait -PassThru
    exit $Process.ExitCode
} catch { exit 1 }

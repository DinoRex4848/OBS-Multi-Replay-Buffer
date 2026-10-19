param([string]$ObsRoot = "C:\Program Files\obs-studio", [string]$Configuration = "Release")
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Repo "build-windows/$Configuration"
$ObsBin = Join-Path $ObsRoot "bin/64bit"
$env:OBS_ROOT = $ObsRoot
$env:PATH = "$ObsBin;$env:PATH"
foreach ($Helper in @("obs-nvenc-test.exe", "obs-ffmpeg-mux.exe")) {
    Copy-Item (Join-Path $ObsBin $Helper) $Build -Force
}
foreach ($Codec in @("av1", "h264", "hevc")) {
    $Output = Join-Path $Repo "build-windows/probe-$Codec"
    & (Join-Path $Build "obs_probe.exe") (Join-Path $Build "multi-replay-writer.exe") $Output $Codec full
    if ($LASTEXITCODE -ne 0) { throw "$Codec integration probe failed" }
    Write-Host "Inspect and decode clips in $Output using scripts/verify-clips.py."
}

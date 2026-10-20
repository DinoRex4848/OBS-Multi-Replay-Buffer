# Pinned official compiler; safe to run on an isolated Windows builder.
$ErrorActionPreference = "Stop"
$Download = Join-Path $env:TEMP "obs-replay-innosetup-6.7.3.exe"
Invoke-WebRequest "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe" -OutFile $Download
if ((Get-FileHash $Download -Algorithm SHA256).Hash -ne "9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732") {
    throw "Inno Setup compiler checksum mismatch"
}
$Process = Start-Process -FilePath $Download -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-" -Wait -PassThru
if ($Process.ExitCode -ne 0) { throw "Inno Setup installation failed" }

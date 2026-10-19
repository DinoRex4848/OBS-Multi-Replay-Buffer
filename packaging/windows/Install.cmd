@echo off
setlocal
if not exist "%~dp0Install.ps1" (
    echo Extract the entire ZIP before running Install.cmd.
    pause
    exit /b 1
)
set "REPLAY_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "REPLAY_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%REPLAY_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Action Install -Mode Standard
set "REPLAY_EXIT=%ERRORLEVEL%"
pause
exit /b %REPLAY_EXIT%

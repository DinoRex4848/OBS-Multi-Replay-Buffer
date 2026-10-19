@echo off
setlocal
set "REPLAY_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "REPLAY_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%REPLAY_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Action Uninstall -Mode Standard
set "REPLAY_EXIT=%ERRORLEVEL%"
pause
exit /b %REPLAY_EXIT%

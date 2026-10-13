@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\windows\run.ps1" %*
exit /b %ERRORLEVEL%

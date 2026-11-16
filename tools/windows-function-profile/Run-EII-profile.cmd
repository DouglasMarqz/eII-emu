@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Record-EII-profile.ps1"
if errorlevel 1 pause

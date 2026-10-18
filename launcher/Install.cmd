@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Bootstrap.ps1"
if errorlevel 1 pause

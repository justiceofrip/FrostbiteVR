@echo off
rem Per-process policy only; Windows and user execution policies are unchanged.
echo BC2 VR installer - choose where to install in the folder window.
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Bootstrap.ps1"
if errorlevel 1 (
  echo Installation did not complete. The error above explains what to change.
  pause
  exit /b 1
)

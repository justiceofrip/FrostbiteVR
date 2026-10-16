@echo off
setlocal
set "FVR_PWSH="
for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do if not defined FVR_PWSH set "FVR_PWSH=%%P"
if not defined FVR_PWSH if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "FVR_PWSH=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not defined FVR_PWSH (
  echo Install PowerShell 7.5 or newer from Microsoft, then run this installer again.
  echo https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows
  pause
  exit /b 1
)
"%FVR_PWSH%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Package.ps1"
if errorlevel 1 (
  echo Installation did not complete. See the message above.
) else (
  echo Installed. Open BC2 VR on your desktop.
)
pause

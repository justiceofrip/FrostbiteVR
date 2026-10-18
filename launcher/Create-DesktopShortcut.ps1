#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
$settings=Read-FvrJson (Join-Path $InstallRoot 'data/settings.json')
$active=Read-FvrJson (Join-Path $InstallRoot 'active.json')
$null=Test-FvrPackage (Get-FvrChild $InstallRoot ('versions/'+$active.current)) $active.current
$entry=Join-Path $InstallRoot 'Start-Launcher.ps1'
if(-not(Test-Path -LiteralPath $entry)){throw 'Launcher entry is missing.'}
$desktop=[Environment]::GetFolderPath('Desktop')
$shell=New-Object -ComObject WScript.Shell
$path=Join-Path $desktop 'BC2 VR.lnk'
$shortcut=$shell.CreateShortcut($path)
$shortcut.TargetPath=Join-Path $PSHOME 'pwsh.exe'
$shortcut.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "'+$entry+'"'
$shortcut.WorkingDirectory=$InstallRoot
$shortcut.Description='Launch Battlefield Bad Company 2 in VR and manage hotfixes.'
$shortcut.WindowStyle=7
if($settings.game_path -and (Test-Path -LiteralPath (Join-Path $settings.game_path 'BFBC2Game.exe'))){$shortcut.IconLocation=(Join-Path $settings.game_path 'BFBC2Game.exe')+',0'}
$shortcut.Save()
[pscustomobject]@{shortcut=$path;target=$shortcut.TargetPath;arguments=$shortcut.Arguments}|ConvertTo-Json

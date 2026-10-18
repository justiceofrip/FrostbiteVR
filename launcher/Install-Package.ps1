#requires -Version 7.5
param([string]$InstallRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Launcher.Storage.ps1')
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'bc2vr-update.json') -Raw|ConvertFrom-Json
if($manifest.asset -notmatch '^BC2VR-\d+\.\d+\.\d+-win64\.zip$'){throw 'Invalid package filename.'}
if($InstallRoot){
    & (Join-Path $PSScriptRoot 'Install-BC2VRLauncher.ps1') -InstallRoot $InstallRoot -PackageZip (Join-Path $PSScriptRoot $manifest.asset) -ManifestPath (Join-Path $PSScriptRoot 'bc2vr-update.json')
    return
}
$selected=Select-FvrInstallFolder
if(-not $selected){return}
& (Join-Path $PSScriptRoot 'Install-BC2VRLauncher.ps1') -InstallRoot $selected -PackageZip (Join-Path $PSScriptRoot $manifest.asset) -ManifestPath (Join-Path $PSScriptRoot 'bc2vr-update.json')

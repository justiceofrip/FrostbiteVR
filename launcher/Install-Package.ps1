#requires -Version 7.5
param([string]$InstallRoot)
$ErrorActionPreference='Stop'
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'bc2vr-update.json') -Raw|ConvertFrom-Json
if($manifest.asset -notmatch '^BC2VR-\d+\.\d+\.\d+-win64\.zip$'){throw 'Invalid package filename.'}
if($InstallRoot){
    & (Join-Path $PSScriptRoot 'Install-BC2VRLauncher.ps1') -InstallRoot $InstallRoot -PackageZip (Join-Path $PSScriptRoot $manifest.asset) -ManifestPath (Join-Path $PSScriptRoot 'bc2vr-update.json')
    return
}
Add-Type -AssemblyName System.Windows.Forms
$picker=[Windows.Forms.FolderBrowserDialog]::new()
$picker.Description='Choose a folder on your data drive. BC2 VR will create a BC2VR-Launcher folder here (not on the Windows drive).'
try{
    if($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){return}
    & (Join-Path $PSScriptRoot 'Install-BC2VRLauncher.ps1') -InstallRoot (Join-Path $picker.SelectedPath 'BC2VR-Launcher') -PackageZip (Join-Path $PSScriptRoot $manifest.asset) -ManifestPath (Join-Path $PSScriptRoot 'bc2vr-update.json')
}finally{$picker.Dispose()}

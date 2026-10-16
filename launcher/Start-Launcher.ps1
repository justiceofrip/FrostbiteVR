#requires -Version 7.5
[CmdletBinding()]
param([string]$InstallRoot=$PSScriptRoot, [switch]$SmokeTest, [string]$PreviewPath,
      [ValidateSet('Validate','Stop','Check')][string]$SmokeAction)
$ErrorActionPreference='Stop'
try {
    $active=Get-Content -LiteralPath (Join-Path $InstallRoot 'active.json') -Raw|ConvertFrom-Json
    if($active.current -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}$'){throw 'Invalid installed version.'}
    $entry=Join-Path $InstallRoot ('versions/'+$active.current+'/app/Launcher.ps1')
    $options=@{InstallRoot=$InstallRoot;SmokeTest=$SmokeTest;PreviewPath=$PreviewPath}
    if($SmokeAction){$options.SmokeAction=$SmokeAction}
    & $entry @options
}catch{
    Add-Type -AssemblyName PresentationFramework
    [Windows.MessageBox]::Show($_.Exception.Message,'BC2 VR launcher')|Out-Null
    if($SmokeTest){throw}
}

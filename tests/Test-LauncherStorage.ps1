#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$script:checks=0;$script:free=11GB;$script:ready=$true;$script:kind=[IO.DriveType]::Fixed
$script:created=@();$script:queries=@();$script:links=@()
$saved=@{};foreach($name in 'TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE'){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$userTemp=[Environment]::GetEnvironmentVariable('TEMP','User');$machineTemp=[Environment]::GetEnvironmentVariable('TEMP','Machine')
function Check([bool]$Value,[string]$Name){if(-not $Value){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$caught=$null;try{& $Work|Out-Null}catch{$caught=$_.Exception.Message};Check ($null -ne $caught) $Name;return $caught}
# Fake disk and directory boundaries only. Production path selection, required
# bytes, diagnostics and process environment behavior run unchanged. No C I/O.
function Get-FvrDriveInfo([string]$DriveRoot){$script:queries+=$DriveRoot;return [pscustomobject]@{Name=$DriveRoot;IsReady=$script:ready;DriveType=$script:kind;AvailableFreeSpace=$script:free}}
function Assert-FvrStorageNoLinks([string]$Path){$script:links+=$Path}
function New-FvrStorageDirectory([string]$Path){$script:created+=$Path}
function Picker([string]$Path,[string]$Result='OK'){
 $p=[pscustomobject]@{Description='';SelectedPath=$Path;Result=$Result;Disposed=$false}
 $p|Add-Member ScriptMethod ShowDialog {$this.Result};$p|Add-Member ScriptMethod Dispose {$this.Disposed=$true};return $p
}
try{
 Assert-FvrFreeSpace 'C:\Games\BC2VR' 64MB
 Check ($script:queries[-1] -eq 'C:\') '11 GiB system drive admits current install working set'
 $script:free=1GB;Assert-FvrFreeSpace 'C:\Games\BC2VR' (150MB+512MB)
 Check $true '1 GiB admits bounded portable PowerShell archive and extraction'
 $script:free=900MB;Assert-FvrFreeSpace 'D:\BC2VR' 768MB
 Check ($script:queries[-1] -eq 'D:\') 'runtime peak plus128 MiB reserve fits900 MiB'
 $script:free=512MB;Assert-FvrFreeSpace 'C:\Games\BC2VR' 1MB
 Check $true 'exact512 MiB minimum admits small install'
 $script:free=400MB;$errorText=Reject {Assert-FvrFreeSpace 'C:\Games\BC2VR' 1MB} 'insufficient selected-drive space rejects'
 Check ($errorText -match '512' -and $errorText -match '400' -and $errorText -match 'C:') 'space failure explains required and available on selected drive'
 $script:free=11GB;$script:ready=$false;$null=Reject {Assert-FvrFreeSpace 'C:\Games\BC2VR' 1MB} 'unready drive rejects'
 $script:ready=$true;$script:kind=[IO.DriveType]::Network;$null=Reject {Get-FvrInstallDrive 'Z:\BC2VR'} 'mapped network drive rejects'
 $script:kind=[IO.DriveType]::Fixed
 foreach($path in '\\server\share\BC2VR','relative\BC2VR','C:relative'){$null=Reject {Get-FvrInstallDrive $path} 'non-local or relative root rejects'}
 $null=Reject {Assert-FvrFreeSpace 'C:\BC2VR' -1} 'negative working-space budget rejects'
 $p=Picker 'C:\Games\My BC2 VR';$chosen=Select-FvrInstallFolder $p
 Check ($chosen -eq 'C:\Games\My BC2 VR' -and $p.Disposed) 'picker returns exact chosen C folder and disposes dialog'
 $p=Picker 'C:\';Check ((Select-FvrInstallFolder $p) -eq 'C:\BC2VR-Launcher') 'drive-root selection creates a product subfolder'
 $p=Picker 'C:\Unused' Cancel;Check ($null -eq (Select-FvrInstallFolder $p) -and $p.Disposed -and $script:created.Count -eq 0) 'cancel creates nothing and closes normally'
 $scratch=Set-FvrLauncherStorage 'C:\Games\My BC2 VR'
 Check ($script:created.Count -eq 1 -and $script:created[0] -eq 'C:\Games\My BC2 VR\data\temp' -and $script:links[0] -eq $script:created[0]) 'chosen C scratch passes link guard and mocked creation boundary'
 Check ($env:TEMP -eq $scratch -and $env:TMP -eq $scratch -and $env:TMPDIR -eq $scratch) 'temporary storage is process-local under chosen root'
 Check ([Environment]::GetEnvironmentVariable('TEMP','User') -eq $userTemp -and [Environment]::GetEnvironmentVariable('TEMP','Machine') -eq $machineTemp) 'user and machine environment remain unchanged'
 Write-Host "$script:checks public storage checks passed; all disk/C writes mocked; no download, UI, game or native action."
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}

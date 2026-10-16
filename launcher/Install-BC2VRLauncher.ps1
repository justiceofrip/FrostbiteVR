#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallRoot,
      [string]$PackageZip, [string]$ManifestPath,
      [switch]$FromSource, [string]$Version='0.1.0',
      [string]$LocalProfile, [string]$GamePath,
      [switch]$NoShortcut)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
$null=Set-FvrLauncherStorage $InstallRoot
$lock=Enter-FvrLock $InstallRoot
try{
    Assert-FvrIdle
    if($FromSource -and ($PackageZip -or $ManifestPath)){throw 'Choose a package install or FromSource, not both.'}
    $profile=$null
    if($LocalProfile){
        $profile=Read-FvrJson $LocalProfile
        foreach($key in @('launch','stop','session_record','label','pins')){if(-not $profile[$key]){throw "Local profile is missing $key"}}
        foreach($pin in $profile.pins){if((Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256){throw 'Local profile hash mismatch.'}}
    }
    if($FromSource){
        # Install these interpreted text files directly for local development.
        # No compiler, runtime copy, native build, archive or TEMP output.
        $null=Assert-FvrVersion $Version;Assert-FvrFreeSpace $InstallRoot 1MB
        $target=Get-FvrChild $InstallRoot ('versions/'+$Version)
        if(Test-Path -LiteralPath $target){throw 'This version is already installed.'}
        $null=New-Item -ItemType Directory -Path (Join-Path $target 'app') -Force
        try{
            $entries=@(foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1')){
                $dest=Join-Path $target ('app/'+$name)
                Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $dest
                @{path=('app/'+$name);sha256=(Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant()}
            })
            Write-FvrJson (Join-Path $target 'package.json') @{schema='fvr.desktop.package.v1';version=$Version;runtime_entry=$null;files=$entries}
            $null=Test-FvrPackage $target $Version
            $previous=if(Test-Path -LiteralPath (Join-Path $InstallRoot 'active.json')){(Read-FvrJson (Join-Path $InstallRoot 'active.json')).current}else{$null}
            Write-FvrJson (Join-Path $InstallRoot 'active.json') @{current=$Version;previous=$previous}
        }catch{Remove-FvrOwnedTree $InstallRoot ('versions/'+$Version);throw}
        $manifest=@{version=$Version}
    }else{
        if(-not $PackageZip -or -not $ManifestPath){throw 'Supply PackageZip and ManifestPath, or FromSource.'}
        $manifest=Read-FvrJson $ManifestPath
        Install-FvrPackage $InstallRoot $PackageZip $manifest
    }
    $data=Join-Path $InstallRoot 'data';$null=New-Item -ItemType Directory -Path $data -Force
    $settingsPath=Join-Path $data 'settings.json'
    $settings=if(Test-Path -LiteralPath $settingsPath){Read-FvrJson $settingsPath}else{@{schema='fvr.desktop.settings.v1';game_path='';local_backend=$null}}
    if($GamePath){$settings.game_path=[IO.Path]::GetFullPath($GamePath)}
    if($LocalProfile){
        $settings.local_backend=$profile
    }
    Write-FvrJson $settingsPath $settings
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-Launcher.ps1') -Destination (Join-Path $InstallRoot 'Start-Launcher.ps1') -Force
    if(-not $NoShortcut){
        $desktop=[Environment]::GetFolderPath('Desktop')
        $shell=New-Object -ComObject WScript.Shell
        $shortcut=$shell.CreateShortcut((Join-Path $desktop 'BC2 VR.lnk'))
        $shortcut.TargetPath=Join-Path $PSHOME 'pwsh.exe'
        $shortcut.Arguments='-NoProfile -NonInteractive -STA -WindowStyle Hidden -File "'+(Join-Path $InstallRoot 'Start-Launcher.ps1')+'"'
        $shortcut.WorkingDirectory=$InstallRoot
        $shortcut.Description='Launch Battlefield Bad Company 2 in VR, manage hotfixes and stop VR.'
        $shortcut.WindowStyle=7
        $gameExe=if($settings.game_path){Join-Path $settings.game_path 'BFBC2Game.exe'}else{''}
        if($gameExe -and (Test-Path -LiteralPath $gameExe)){$shortcut.IconLocation=$gameExe+',0'}
        $shortcut.Save()
    }
    [pscustomobject]@{installed=$true;version=$manifest.version;root=$InstallRoot;desktop_shortcut=(-not $NoShortcut);runtime='existing local profile or packaged runtime';game_started=$false}|ConvertTo-Json
}finally{$lock.Dispose()}

#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$testRoot=Join-Path $repo ('test-temp/installer-settings-'+[guid]::NewGuid().ToString('N'))
$originalEnvironment=@{};foreach($name in 'TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE'){$originalEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$script:checks=0
function Check([bool]$Value,[string]$Name){if(-not $Value){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Install([string]$Version){
    $result=& (Join-Path $repo 'launcher/Install-BC2VRLauncher.ps1') -InstallRoot $testRoot -FromSource -Version $Version -NoShortcut
    return ($result|ConvertFrom-Json -AsHashtable)
}
try{
    $null=Set-FvrLauncherStorage $testRoot
    $settings=Join-Path $testRoot 'data/settings.json'
    $broken='{"game_path":"G:\Games\BC2", "local_backend":broken}'
    [IO.File]::WriteAllText($settings,$broken,[Text.UTF8Encoding]::new($false))
    $before=(Get-FileHash -LiteralPath $settings).Hash
    $result=Install '0.1.6'
    Check ($result.installed -and $result.settings_preserved -and $result.settings_notice) 'actual installer completes while explaining unresolved settings'
    Check ((Get-FileHash -LiteralPath $settings).Hash -eq $before) 'malformed original bytes survive install unchanged'
    $backups=@(Get-ChildItem -LiteralPath (Join-Path $testRoot 'data/settings-backups') -File)
    Check ($backups.Count -eq 1 -and (Get-FileHash -LiteralPath $backups[0].FullName).Hash -eq $before) 'exact malformed bytes are backed up'
    Check ((Read-FvrJson (Join-Path $testRoot 'active.json')).current -eq '0.1.6') 'new version becomes active despite malformed settings'
    foreach($name in 'Launcher.Storage.ps1','Launcher.Settings.ps1'){
        Check (Test-Path -LiteralPath (Join-Path $testRoot ('versions/0.1.6/app/'+$name))) ('FromSource includes '+$name)
    }
    $raw='{"game_path":"G:\Games\BC2", "local_backend":{"label":"kept","pins":[]}, "custom":{"enabled":true}}'
    [IO.File]::WriteAllText($settings,$raw,[Text.UTF8Encoding]::new($false))
    $result=Install '0.1.7';$updated=Read-FvrJson $settings
    Check ($result.installed -and -not $result.settings_preserved -and $result.settings_notice -match 'repaired') 'installer accepts unambiguous game-path repair'
    Check ($updated.game_path -eq 'G:\Games\BC2' -and $updated.local_backend.label -eq 'kept' -and $updated.custom.enabled) 'repair preserves backend and unknown settings'
    $deep=@{value='retain deeply nested settings'};foreach($i in 1..24){$deep=@{child=$deep}}
    $valid=@{schema='fvr.desktop.settings.v1';game_path='G:\Games\BC2';local_backend=@{label='kept'};extra=$deep}
    [IO.File]::WriteAllText($settings,($valid|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
    $result=Install '0.1.8';$updated=Read-FvrJson $settings
    $node=$updated.extra;foreach($i in 1..24){$node=$node.child}
    Check ($result.installed -and $node.value -eq 'retain deeply nested settings' -and $updated.local_backend.label -eq 'kept') 'update retains deeply nested unknown settings without serializer truncation'
    Check ((Read-FvrJson (Join-Path $testRoot 'active.json')).previous -eq '0.1.7') 'ordinary previous-version rollback pointer remains available'
    Write-Host "$script:checks installer settings checks passed; G-only files, no shortcuts, downloads or game actions."
}finally{
    foreach($name in $originalEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$originalEnvironment[$name],'Process')}
    if(Test-Path -LiteralPath $testRoot){Remove-FvrOwnedTree (Join-Path $repo 'test-temp') (Split-Path $testRoot -Leaf)}
}

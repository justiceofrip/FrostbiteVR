#requires -Version 7.5
[CmdletBinding()]
param([ValidateSet('Validate','Launch','Stop')][string]$Action,
      [Parameter(Mandatory)][string]$SettingsPath,[Parameter(Mandatory)][string]$StateRoot,
      [switch]$Fresh,[ValidateSet('MainMenu','Campaign')][string]$StartAt='MainMenu',[switch]$ActivateGameWindow)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../app/Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Runtime.Core.ps1')
. (Join-Path $PSScriptRoot 'Dependencies.ps1')
. (Join-Path $PSScriptRoot 'Startup.ps1')
$StateRoot=[IO.Path]::GetFullPath($StateRoot)
$null=Set-FvrLauncherStorage (Split-Path (Split-Path $StateRoot -Parent) -Parent)
$null=New-Item -ItemType Directory -Path $StateRoot -Force
$active=Get-Bc2Session $StateRoot
if($Action -eq 'Stop'){
    if($active -and (Find-Bc2OwnedProcess $active.supervisor)){
        [IO.File]::WriteAllText((Join-Path $active.folder 'stop.request'),'Requested by launcher')
        Write-Output 'VR stop requested. BC2 and SteamVR remain open.'
    }else{Write-Output 'No owned BC2 VR session is running.'}
    return
}
$config=Get-Bc2RuntimeConfig
$settings=Read-FvrJson $SettingsPath
$installation=Test-Bc2GameInstallation $settings $config $StateRoot
$exe=$installation.exe;$gameHash=$installation.sha256
$xr=Get-Bc2OpenXrSelection
Write-FvrJson (Join-Path $StateRoot 'xr-selection.json') $xr
$bundledPython=Join-Path $PSScriptRoot 'python/python.exe'
$python=if(Test-Path -LiteralPath $bundledPython){$bundledPython}else{Resolve-Bc2Tool $env:FVR_PYTHON @('python.exe','python3.exe') 'Python 3.11+'}
& $python -B -c 'import sys; sys.exit(0 if sys.version_info >= (3,11) else 1)'
if($LASTEXITCODE){throw 'Python 3.11 or newer is required.'}
$cache=Get-FvrChild $StateRoot ('cache/'+$config.asset_recipe_sha256)
$cached=Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $gameHash
Assert-Bc2RuntimeSpace $StateRoot
if($Action -eq 'Validate'){
    $null=Get-Bc2PreparationFfmpeg $StateRoot (Join-Path $PSScriptRoot 'config/ffmpeg-download.json') $cached Validate
    @{validated=$true;version=$config.version;source_sha256=$config.source_sha256;assets_ready=$cached;openxr_runtime=$xr.label;headset_checked=$false;game_started=$false}|ConvertTo-Json;return
}
if($active -and (Find-Bc2OwnedProcess $active.supervisor)){Write-Output 'BC2 VR is already running.';return}
if($StartAt -ne 'MainMenu'){throw 'The public launcher starts at the VR main menu. Select Resume Game there.'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 VR session is active. Stop it before launching.'}
$null=Invoke-Bc2OpenXrPreflight $PSScriptRoot $StateRoot $xr
Write-Information 'BC2VR: Preparing local equipment and audio caches…' -InformationAction Continue
$ffmpeg=Get-Bc2PreparationFfmpeg $StateRoot (Join-Path $PSScriptRoot 'config/ffmpeg-download.json') $cached Launch
# A first download can take minutes; acquire process identities only afterward.
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 VR session became active during preparation. Stop it before launching.'}
$games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
if($games.Count -gt 1){throw 'Multiple BC2 games are running; no process was selected.'}
$game=if($games.Count){$games[0]}else{$null}
if($game){
    if($game.Path -ne $exe){throw 'Another BC2 installation is running.'}
    if(-not $Fresh){throw 'Enable Restart BC2 before launch. A fresh game must inherit the physical reload audio settings.'}
    if(-not $game.CloseMainWindow() -or -not $game.WaitForExit(8000)){throw 'BC2 did not exit normally. Close it yourself, then launch again.'}
}
if(-not $cached){
    Write-Output 'Preparing body equipment and reload sounds from your installed game (first launch only)…'
    # Remove only the previous diagnostic, so an import/startup failure cannot
    # display a different attempt's error as the cause of this launch.
    $preparationReport=Get-FvrChild $cache 'preparation.json'
    Assert-FvrNoLinks $preparationReport
    if(Test-Path -LiteralPath $preparationReport){Remove-Item -LiteralPath $preparationReport -Force}
    & $python -B (Join-Path $PSScriptRoot 'tools/prepare_runtime_assets.py') --game $settings.game_path --output $cache --ffmpeg $ffmpeg --recipe $config.asset_recipe_sha256 --game-sha256 $gameHash
    $preparationExit=$LASTEXITCODE
    $preparation=Save-Bc2AssetPreparationResult $cache $StateRoot $config.asset_recipe_sha256 $gameHash $preparationExit (Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $gameHash)
    if($preparation.state -ne 'complete'){throw ('Local asset preparation failed at '+$preparation.stage+': '+$preparation.error+' No game was started. Use Report feedback > Save diagnostics to share the details.')}
}
$relative='sessions/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$folder=Get-FvrChild $StateRoot $relative;$null=New-Item -ItemType Directory -Path $folder
$options=@{schema='bc2vr.session-options.v1';state_root=$StateRoot;relative_folder=$relative;game_path=$settings.game_path;game_sha256=$gameHash;cache=$cache;python=$python;activate_window=[bool]$ActivateGameWindow;version=$config.version;source_sha256=$config.source_sha256;xr_manifest=$xr.manifest;xr_label=$xr.label}
$optionsFile=Join-Path $folder 'options.json';Write-FvrJson $optionsFile $options
$supervisor=Start-Bc2Child (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Session.ps1'),'-OptionsPath',$optionsFile) $folder 'supervisor'
Write-FvrJson (Join-Path $StateRoot 'active-session.json') @{schema='bc2vr.active-session.v1';relative_folder=$relative;supervisor=(Get-Bc2ProcessIdentity $supervisor);version=$config.version}
$deadline=[DateTime]::UtcNow.AddSeconds(180);$lastPhase=''
while([DateTime]::UtcNow -lt $deadline){
    $record=Join-Path $folder 'session.json'
    if(Test-Path -LiteralPath $record){
        $session=Read-FvrJson $record
        if($session.phase -and $session.phase -ne $lastPhase){$lastPhase=$session.phase;Write-Information ('BC2VR: '+$lastPhase) -InformationAction Continue}
        if($session.state -eq 'running'){Write-Output 'BC2 VR is connected; native VR input received.';return}
        if($session.state -eq 'failed'){throw $session.error}
        if($session.state -eq 'stopped'){throw 'VR startup was stopped.'}
    }
    if($supervisor.HasExited){throw "VR supervisor exited. See $folder"}
    Start-Sleep -Milliseconds 200
}
throw "VR startup is still pending. Do not start another session; inspect $folder"

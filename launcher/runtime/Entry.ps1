#requires -Version 7.5
[CmdletBinding()]
param([ValidateSet('Validate','Launch','Stop')][string]$Action,
      [Parameter(Mandatory)][string]$SettingsPath,[Parameter(Mandatory)][string]$StateRoot,
      [switch]$Fresh,[ValidateSet('MainMenu','Campaign')][string]$StartAt='MainMenu',[switch]$ActivateGameWindow,
      [switch]$QualifyChestKnife,[switch]$PrivateSourcePipeline,[switch]$PerformanceMode=$false,[string]$PreAttachSetupSpec,[string]$PreAttachSetupSpecSha256,[string]$PrivateSessionBinding,[string]$PrivateSessionBindingSha256)
$ErrorActionPreference='Stop'
if($PrivateSourcePipeline -and $Action -ne 'Launch'){throw 'Private source pipeline requires Launch.'}
if($PerformanceMode -and $Action -ne 'Launch'){throw 'VR performance mode requires Launch.'}
if($PerformanceMode -and $PrivateSourcePipeline){throw 'VR performance mode and the private source-pipeline override are separate launch options. Select only one.'}
. (Join-Path $PSScriptRoot '../app/Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Runtime.Core.ps1')
. (Join-Path $PSScriptRoot 'Dependencies.ps1')
. (Join-Path $PSScriptRoot 'Startup.ps1')
. (Join-Path $PSScriptRoot 'Resolution.ps1')
. (Join-Path $PSScriptRoot 'GraphicsSettings.ps1')
$StateRoot=[IO.Path]::GetFullPath($StateRoot)
$null=Set-FvrLauncherStorage (Split-Path (Split-Path $StateRoot -Parent) -Parent)
$null=New-Item -ItemType Directory -Path $StateRoot -Force
$active=Get-Bc2Session $StateRoot
if($Action -eq 'Stop'){
    if($active -and (Find-Bc2OwnedProcess $active.supervisor)){
        [IO.File]::WriteAllText((Join-Path $active.folder 'stop.request'),'Requested by launcher')
        $stopMessage='VR stop requested. BC2 and SteamVR remain open; headset shutdown is not yet confirmed.'
        $prior=Get-Bc2SessionRecord $active
        if($prior -and $prior.ContainsKey('performance_mode') -and $prior.performance_mode.requested){$stopMessage+=' Temporary MSAA restoration waits until BC2 closes normally.'}
        [pscustomobject]@{schema='bc2vr.stop-result.v1';outcome='requested';message=$stopMessage}
    }else{
        $recovery=Restore-Bc2GraphicsSettingsLease -StateRoot $StateRoot
        $stopMessage='No owned BC2 VR session is running.'
        if($recovery.status -eq 'pending'){$stopMessage+=' Graphics settings restoration is pending ('+$recovery.reason+'). Close BC2 normally; retained settings will be checked again before another launch.'}
        [pscustomobject]@{schema='bc2vr.stop-result.v1';outcome='no_owned_session';message=$stopMessage}
    }
    return
}
$preAttachSetup=$null
if($PreAttachSetupSpec -or $PreAttachSetupSpecSha256){
    if($Action -ne 'Launch' -or -not [IO.Path]::IsPathFullyQualified($PreAttachSetupSpec) -or $PreAttachSetupSpecSha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'Explicit pre-launch setup path and SHA256 are required.'}
    Assert-FvrNoLinks $PreAttachSetupSpec
    if((Get-FileHash -LiteralPath $PreAttachSetupSpec -Algorithm SHA256).Hash.ToLowerInvariant() -cne $PreAttachSetupSpecSha256){throw 'Pre-launch setup specification changed.'}
    $preAttachSetup=@{path=[IO.Path]::GetFullPath($PreAttachSetupSpec);sha256=$PreAttachSetupSpecSha256}
}
$privateAdmission=$null
if($PrivateSessionBinding -or $PrivateSessionBindingSha256){
    if($Action -ne 'Launch' -or $preAttachSetup){throw 'Private campaign admission requires Launch without pre-attach setup.'}
    . (Join-Path $PSScriptRoot 'PrivateSession.ps1')
    $privateAdmission=Get-Bc2PrivateAdmission $PrivateSessionBinding $PrivateSessionBindingSha256 $PSScriptRoot (Split-Path (Split-Path $StateRoot -Parent) -Parent)
    $config=$privateAdmission.config
}else{$config=Get-Bc2RuntimeConfig}
$settings=Read-FvrJson $SettingsPath
$installation=Test-Bc2GameInstallation $settings $config $StateRoot
$exe=$installation.exe;$gameHash=$installation.sha256
$launcherRoot=Split-Path (Split-Path $StateRoot -Parent) -Parent
$placement=Get-FvrLauncherGamePlacement $launcherRoot $settings.game_path
$placement.utc=[DateTime]::UtcNow.ToString('o');$placement.version=$config.version
Write-FvrJson (Join-Path $StateRoot 'launcher-placement.json') $placement
if(-not $placement.allowed){throw $placement.error}
$xr=Get-Bc2OpenXrSelection
Write-FvrJson (Join-Path $StateRoot 'xr-selection.json') $xr
$bundledPython=Join-Path $PSScriptRoot 'python/python.exe'
$python=if(Test-Path -LiteralPath $bundledPython){$bundledPython}else{Resolve-Bc2Tool $env:FVR_PYTHON @('python.exe','python3.exe') 'Python 3.11+'}
& $python -B -c 'import sys; sys.exit(0 if sys.version_info >= (3,11) else 1)'
if($LASTEXITCODE){throw 'Python 3.11 or newer is required.'}
$cacheRoot=Get-FvrChild $StateRoot 'cache'
$cache=Get-FvrChild $cacheRoot $config.asset_recipe_sha256
$cached=Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $gameHash
Assert-Bc2RuntimeSpace $StateRoot
if($Action -eq 'Validate'){
    $null=Get-Bc2PreparationFfmpeg $StateRoot (Join-Path $PSScriptRoot 'config/ffmpeg-download.json') $cached Validate
    @{validated=$true;version=$config.version;source_sha256=$config.source_sha256;assets_ready=$cached;openxr_runtime=$xr.label;headset_checked=$false;game_started=$false}|ConvertTo-Json;return
}
if($active -and (Find-Bc2OwnedProcess $active.supervisor)){
    $prior=Get-Bc2SessionRecord $active
    if($prior -and $prior.state -in @('failed','stopped','ended')){
        throw 'The previous VR session is finishing cleanup. Wait for it to finish, then enable Restart BC2 before launch or close BC2 normally. Reinstalling BC2VR is unnecessary.'
    }
    Write-Output 'BC2 VR is already running.';return
}
if($StartAt -ne 'MainMenu'){throw 'The public launcher starts at the VR main menu. Select Resume Game there.'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 VR session is active. Stop it before launching.'}
# Recovery applies to every later launch, including performance mode off. Never
# let a pending lease silently become the player's ordinary graphics settings.
$recovery=Restore-Bc2GraphicsSettingsLease -StateRoot $StateRoot
if($Fresh -and $recovery.status -eq 'pending' -and $recovery.reason -eq 'game_alive'){
    # Explicit Restart may close only the exact game bound to this validated
    # lease. Hold its lock through normal closure so a changed journal cannot
    # turn permission to restart one lifetime into permission for another.
    $graphicsPaths=Get-Bc2GraphicsLeasePaths $StateRoot
    $graphicsLock=Enter-Bc2GraphicsLease $graphicsPaths -Deadline ([DateTime]::UtcNow.AddSeconds(5))
    $restartGame=$null
    try {
        $lease=Read-Bc2GraphicsLease $graphicsPaths
        if(-not $lease -or $lease.state -cne 'bound' -or -not $lease.game -or $lease.game_path -ine $exe){throw 'Graphics restart lease is ambiguous; close BC2 normally before launching again.'}
        $restartGame=Get-Bc2ExistingGame $exe $true
        if(-not $restartGame){throw 'The leased BC2 game changed before restart; launch again after recovery.'}
        $identity=Get-Bc2ProcessIdentity $restartGame -RequireLive $true
        if($identity.pid -ne $lease.game.pid -or $identity.path -ine $lease.game.path -or
           [DateTimeOffset]::Parse($identity.start_utc).UtcDateTime -ne [DateTimeOffset]::Parse($lease.game.start_utc).UtcDateTime){throw 'The leased BC2 game identity changed; no game was closed.'}
        # A live-game block precedes settings inspection in ordinary recovery.
        # Check for a conflict before asking the player-owned game to close.
        Assert-FvrNoLinks $lease.settings_path
        if((Get-Item -LiteralPath $lease.settings_path).Length -gt 1MB){throw 'Graphics settings are oversized; no game was closed.'}
        $currentGraphics=[IO.File]::ReadAllBytes($lease.settings_path)
        if((Get-Bc2GraphicsHash $currentGraphics) -cne $lease.original_sha256 -and $lease.original_sha256 -cne $lease.prepared_sha256){
            $restorePlan=Convert-Bc2GraphicsMsaa $currentGraphics $lease.original_msaa
            if($restorePlan.value -cne '0'){throw 'Graphics MSAA changed after preparation; no game was closed and the backup is retained.'}
        }
        if(-not $restartGame.CloseMainWindow() -or -not $restartGame.WaitForExit(8000)){throw 'BC2 did not exit normally. Close it yourself, then launch again.'}
    } finally {
        if($restartGame){$restartGame.Dispose()}
        $graphicsLock.Dispose()
    }
    $recovery=Restore-Bc2GraphicsSettingsLease -StateRoot $StateRoot
}
Write-FvrJson (Join-Path $StateRoot 'graphics-settings-recovery.json') $recovery
if($recovery.status -notin @('none','restored')){throw ('Graphics settings restoration is pending ('+$recovery.reason+'). Close BC2 normally before launching again. '+$(if($recovery.ContainsKey('message')){$recovery.message}))}
if($PerformanceMode){
    if([Environment]::GetEnvironmentVariable('FVR_PRIVATE_HOST_COPY_SERVICE_MS','Process') -notin @($null,'','0')){throw 'VR performance mode requires the separate host copy service disabled. BC2 has not been started.'}
    $graphicsPath=Get-Bc2GraphicsSettingsPath;Assert-FvrNoLinks $graphicsPath
    if((Get-Item -LiteralPath $graphicsPath).Length -gt 1MB){throw 'VR performance mode graphics settings are oversized. BC2 has not been started.'}
    $null=Convert-Bc2GraphicsMsaa ([IO.File]::ReadAllBytes($graphicsPath))
}
# Reject an unchanged live game before opening another headset session. Repeat
# the exact selection after preparation, which may take minutes on first use.
$null=Get-Bc2ExistingGame $exe ([bool]$Fresh)
$probeOriginal=@{};$probePresent=[Environment]::GetEnvironmentVariables('Process')
try {
    # The probe always uses the ordinary protocol. A caller's diagnostic flags
    # cannot select a source pipeline before the recommended size is known.
    foreach($key in @('FVR_PRIVATE_TWO_SOURCE_SLOTS','FVR_PRIVATE_THREE_SOURCE_SLOTS','FVR_PRIVATE_SOURCE_LOOKAHEAD','FVR_PRIVATE_EXPORT_OVERLAP')){
        $probeOriginal[$key]=@{present=$probePresent.Contains($key);value=[Environment]::GetEnvironmentVariable($key,'Process')}
        [Environment]::SetEnvironmentVariable($key,'0','Process')
    }
    $preflight=Invoke-Bc2OpenXrPreflight $PSScriptRoot $StateRoot $xr
} finally {
    foreach($key in $probeOriginal.Keys){
        if($probeOriginal[$key].present){[Environment]::SetEnvironmentVariable($key,$probeOriginal[$key].value,'Process')}
        else {[Environment]::SetEnvironmentVariable($key,[NullString]::Value,'Process')}
    }
}
$renderPlan=Get-Bc2ResolutionPlan $preflight
if($PerformanceMode){
    # SceneDepthImportPolicy::PipelineTextureBytes, N=3/S=1; the native
    # descriptor, adapter budget and 64 MiB headroom checks remain authoritative.
    $requiredBytes=[long]$renderPlan.width*[long]$renderPlan.height*4*(3+2*3+4*3*1)
    if($requiredBytes -gt 768MB){throw 'VR performance mode exceeds the 768 MiB three-source budget at the headset recommended resolution. Disable performance mode to use the ordinary pipeline; no graphics settings were changed and BC2 has not been started.'}
    Write-Information 'BC2VR: VR performance mode will temporarily disable MSAA; headset resolution stays unchanged. Settings restore after BC2 closes.' -InformationAction Continue
}
Write-Information 'BC2VR: Preparing local equipment and audio caches…' -InformationAction Continue
$ffmpeg=Get-Bc2PreparationFfmpeg $StateRoot (Join-Path $PSScriptRoot 'config/ffmpeg-download.json') $cached Launch
# A first download can take minutes; acquire process identities only afterward.
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 VR session became active during preparation. Stop it before launching.'}
$game=Get-Bc2ExistingGame $exe ([bool]$Fresh)
if($game){
    if(-not $game.CloseMainWindow() -or -not $game.WaitForExit(8000)){throw 'BC2 did not exit normally. Close it yourself, then launch again.'}
}
if(-not $cached){
    Write-Output 'Preparing body equipment and reload sounds from your installed game (first launch only)…'
    # Remove only the previous diagnostic, so an import/startup failure cannot
    # display a different attempt's error as the cause of this launch.
    $preparationReport=Get-FvrChild $cache 'preparation.json'
    Assert-FvrNoLinks $preparationReport
    if(Test-Path -LiteralPath $preparationReport){Remove-Item -LiteralPath $preparationReport -Force}
    & $python -B (Join-Path $PSScriptRoot 'tools/prepare_runtime_assets.py') --game $settings.game_path --output $cache --managed-cache-root $cacheRoot --ffmpeg $ffmpeg --recipe $config.asset_recipe_sha256 --game-sha256 $gameHash
    $preparationExit=$LASTEXITCODE
    $preparation=Save-Bc2AssetPreparationResult $cache $StateRoot $config.asset_recipe_sha256 $gameHash $preparationExit (Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $gameHash)
    if($preparation.state -ne 'complete'){throw ('Local asset preparation failed at '+$preparation.stage+': '+$preparation.error+' No game was started. Use Report feedback > Save diagnostics to share the details.')}
}
$relative='sessions/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$folder=Get-FvrChild $StateRoot $relative;$null=New-Item -ItemType Directory -Path $folder
# The owned report directory exists now; no supervisor/native startup can race publication.
$privateMarkers=if($privateAdmission){New-Bc2PrivateMarkers $folder}else{$null}
$options=@{schema='bc2vr.session-options.v1';state_root=$StateRoot;relative_folder=$relative;game_path=$settings.game_path;game_sha256=$gameHash;steam_compatibility=($installation.ContainsKey('experimental_steam') -and $installation.experimental_steam);cache=$cache;python=$python;activate_window=[bool]$ActivateGameWindow;performance_mode=[bool]$PerformanceMode;version=$config.version;source_sha256=$config.source_sha256;xr_manifest=$xr.manifest;xr_label=$xr.label;render_plan=$renderPlan}
if($privateAdmission){
    $options.private_campaign=@{binding=$privateAdmission.binding;markers=$privateMarkers;diagnostic_only=$true;
        source_commit=$privateAdmission.specification.source_commit;normal_artifacts_receipt=$privateAdmission.specification.normal_artifacts_receipt;
        private_artifacts_receipt=$privateAdmission.specification.private_artifacts_receipt}
}
if($QualifyChestKnife){$options.qualify_chest_knife=$true}
# The probe has already completed. Only the dedicated supervisor may activate
# this option; inheriting the three-source flag into --probe would be invalid.
if($PrivateSourcePipeline){$options.private_source_pipeline=$true}
if($preAttachSetup){$options.pre_attach_setup=$preAttachSetup}
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

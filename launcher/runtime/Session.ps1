#requires -Version 7.5
param([Parameter(Mandatory)][string]$OptionsPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../app/Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Runtime.Core.ps1')
. (Join-Path $PSScriptRoot 'Startup.ps1')
. (Join-Path $PSScriptRoot 'Resolution.ps1')
$options=Read-FvrJson $OptionsPath
$folder=Get-FvrChild $options.state_root $options.relative_folder
Assert-FvrNoLinks $folder
if([IO.Path]::GetFullPath($OptionsPath) -ne (Join-Path $folder 'options.json')){throw 'Session options escaped the owned folder.'}
$null=Set-FvrLauncherStorage (Split-Path (Split-Path $options.state_root -Parent) -Parent)
$state=@{schema='bc2vr.session.v1';state='starting';phase='Starting BC2 and waiting for its main menu…';version=$options.version;source_sha256=$options.source_sha256;started_utc=[DateTime]::UtcNow.ToString('o');game=$null;host=$null;trace=$null;error=$null;supervisor=(Get-Bc2ProcessIdentity (Get-Process -Id $PID))}
$stateFile=Join-Path $folder 'session.json';$stopFile=Join-Path $folder 'stop.request'
$hostProcess=$null;$game=$null;$trace=$null;$resolutionProcess=$null
$resolutionLease=$null;$resolutionAttempted=$false;$resolutionReleaseProcess=$null
function Save-State {Write-FvrJson $stateFile $state}
function Save-NativeOutcome {
    if(-not $trace -or -not $trace.HasExited -or -not $state.game){return $null}
    $state.native_bootstrap_exited=$true
    $result=Read-Bc2NativeRunOutcome (Join-Path $folder 'native.stdout.log') ([uint32]$state.game.pid)
    if(-not $result){$state.native_outcome='unavailable';return $null}
    $state.native_outcome=$result.outcome
    $state.native_remote_thread_exit_observed=$result.remote_thread_exit_observed
    $state.native_probe_exit=$result.probe_exit;$state.native_game_exit_code=$result.game_exit_code
    $state.native_report_bytes=$result.report_bytes;$state.native_report_nonempty=$result.report_nonempty
    if($result.outcome -ceq 'game_exited'){
        # Target process teardown retires all code; it does not prove that the
        # probe ran its cleanup or completed its report before that teardown.
        $state.native_drained=$false;$state.native_cleanup='not_observed_game_exit'
        if($null -ne $result.game_exit_code -and $result.game_exit_code -ne 0 -and -not $state.error){
            $state.state='failed'
            $state.error="BC2 exited with process code $($result.game_exit_code) before the native probe return was observed. Use Report feedback to save diagnostics."
        }
    }
    return $result
}
function Stop-OwnedHost {
    if($hostProcess -and -not $hostProcess.HasExited){
        [IO.File]::WriteAllText($stopFile,'Session supervisor cleanup')
        if(-not $hostProcess.WaitForExit(10000)){$hostProcess.Kill();$null=$hostProcess.WaitForExit(2000);$state.host_forced_stop=$true}
    }
}
function Complete-ResolutionSession {
    if(-not $resolutionAttempted){return}
    # The saved lease exists before configure starts. A missing/malformed setup
    # report must not orphan a guard that the native worker already armed.
    if($game.HasExited){$state.resolution_cleanup='game_exited';$state.resolution_guard_active=$false;return}
    if(-not(Find-Bc2OwnedProcess $state.game)){throw 'Resolution cleanup cannot verify the original BC2 process.'}
    if($state.ContainsKey('native_outcome') -and $state.native_outcome -ceq 'game_exited'){throw 'Native outcome disagrees with the live original game; resolution cleanup remains unconfirmed.'}
    if(($resolutionProcess -and -not $resolutionProcess.HasExited) -or
       ($trace -and (-not $trace.HasExited -or $trace.ExitCode -ne 0))){
        throw 'Resolution guard cleanup is pending because native cleanup has not been confirmed.'
    }
    if($hostProcess -and -not $hostProcess.HasExited){throw 'Resolution guard cleanup is pending while the headset host is stopping.'}
    $state.phase='Releasing the render-resolution guard for this VR session…';Save-State
    $releaseReport=Join-Path $folder 'resolution-release.json'
    $releaseArguments=@('--operation','release','--game',$options.game_path,'--pid',[string]$resolutionLease.pid,
        '--creation-filetime',$resolutionLease.creation_filetime,'--dll',(Join-Path $PSScriptRoot 'bin/BC2ResolutionBootstrap.dll'),
        '--dll-sha256',$resolutionLease.dll_sha256,'--supervisor-pid',[string]$resolutionLease.supervisor_pid,
        '--supervisor-creation-filetime',$resolutionLease.supervisor_creation_filetime,'--session-token',$resolutionLease.session_token,
        '--report',$releaseReport)
    $resolutionReleaseProcess=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2NativeResolution.exe') $releaseArguments $folder 'resolution-release'
    $state.resolution_release_helper=Get-Bc2ProcessIdentity $resolutionReleaseProcess;Save-State
    # The release callback may still be draining; never terminate the helper.
    if(-not $resolutionReleaseProcess.WaitForExit(10000)){throw 'Resolution guard cleanup is still pending. Close BC2 before another launch.'}
    if($resolutionReleaseProcess.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $releaseReport -PathType Leaf)){
        throw (Get-Bc2StartupFailure $folder 'resolution-release' 'Resolution guard release could not be confirmed.')
    }
    $released=Read-FvrJson $releaseReport
    Assert-Bc2ResolutionIdentity $released $resolutionLease 'release'
    if($released.ContainsKey('game_exited') -and $released.game_exited -is [bool] -and $released.game_exited){
        Assert-Bc2ResolutionFlag $released 'passed' $true
        if(-not $game.HasExited){throw 'Resolution release reported game exit without the original process exiting.'}
        $state.resolution_cleanup='game_exited';$state.resolution_guard_active=$false;$state.resolution_cleanup_pending=$false;return
    }
    Assert-Bc2ResolutionReleased $released $resolutionLease
    $state.resolution_cleanup=$(if($released.guard_never_installed){'never_installed'}else{'released'})
    $state.resolution_guard_released=$released.guard_released
    $state.resolution_guard_active=$false
    $state.resolution_cleanup_pending=$false
}
try {
    Save-State
    $config=Get-Bc2RuntimeConfig
    if($options.source_sha256 -ne $config.source_sha256 -or $options.version -ne $config.version){throw 'Runtime changed between launch and session.'}
    if(-not $options.ContainsKey('render_plan') -or $options.render_plan -isnot [hashtable]){throw 'The headset render plan is missing. BC2 has not been started.'}
    $renderPlan=$options.render_plan;Assert-Bc2ResolutionPlan $renderPlan
    $state.render_plan=$renderPlan
    Assert-Bc2RuntimeSpace $options.state_root
    $exe=Join-Path $options.game_path 'BFBC2Game.exe'
    if((Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant() -ne $options.game_sha256){throw 'BC2 executable changed before launch.'}
    $steamCompatibility=$options.ContainsKey('steam_compatibility') -and $options.steam_compatibility -is [bool] -and $options.steam_compatibility
    $admission=Test-Bc2ExecutableAdmission -Exe $exe -Hash $options.game_sha256 -Config $config -ExperimentalSteam:$steamCompatibility
    $state.executable_profile=$admission.profile;$state.steam_compatibility=$admission.experimental_steam
    [string[]]$menuCompatibilityArguments=if($admission.experimental_steam){@('--experimental-steam')}else{@()}
    if(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'BC2 or a VR helper started concurrently; launch cancelled.'}
    if(-not(Test-Bc2AssetCache $options.cache $config.asset_recipe_sha256 $options.game_sha256)){throw 'Prepared assets changed before launch.'}
    # Audio settings are attached only to this new game process, never to the
    # machine/user environment or a guessed already-running game.
    $game=Start-Process -FilePath $exe -WorkingDirectory $options.game_path -Environment (Get-Bc2AudioEnvironment $options.cache) -PassThru
    $state.game=Get-Bc2ProcessIdentity $game;$state.audio_environment_inherited=$true;Save-State
    $creation=$game.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
    & $options.python -B (Join-Path $PSScriptRoot 'tools/wait_main_menu.py') --pid $game.Id --creation-filetime $creation --exe $exe --expected-sha256 $options.game_sha256 --output (Join-Path $folder 'menu-ready.json') @menuCompatibilityArguments *> (Join-Path $folder 'menu-ready.log')
    if($LASTEXITCODE){
        $menuError='BC2 main menu or Direct3D 11 renderer did not become ready. The game remains open.'
        $menuRecord=Join-Path $folder 'menu-ready.json'
        if(Test-Path -LiteralPath $menuRecord){try{$observed=Read-FvrJson $menuRecord;if($observed.error){$menuError+=' '+$observed.error}}catch{}}
        throw (Get-Bc2StartupFailure $folder 'menu-ready' $menuError)
    }
    $menuReady=Read-FvrJson (Join-Path $folder 'menu-ready.json')
    if(-not $menuReady.passed -or $menuReady.width -le 0 -or $menuReady.height -le 0 -or $menuReady.width -gt 16384 -or $menuReady.height -gt 16384 -or [long]$menuReady.width*$menuReady.height -gt 32000000){throw 'The live game resolution could not be verified. No VR adapter was attached.'}
    $state.initial_width=$menuReady.width;$state.initial_height=$menuReady.height
    if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
    $state.phase='Using the headset render resolution for this VR session; desktop resolution changes resume after VR stops.'
    $resolutionDll=Join-Path $PSScriptRoot 'bin/BC2ResolutionBootstrap.dll'
    $resolutionLease=New-Bc2ResolutionLease $state.game $state.supervisor (Get-FileHash -LiteralPath $resolutionDll -Algorithm SHA256).Hash.ToLowerInvariant()
    $state.resolution_lease=$resolutionLease
    $state.resolution_guard_released=$false
    Write-FvrJson (Join-Path $folder 'resolution-lease.json') $resolutionLease
    Save-State
    $resolutionReport=Join-Path $folder 'resolution-native.json'
    $resolutionArguments=@('--operation','configure','--game',$options.game_path,'--pid',[string]$game.Id,'--creation-filetime',$creation,
        '--dll',$resolutionDll,'--dll-sha256',$resolutionLease.dll_sha256,
        '--supervisor-pid',[string]$resolutionLease.supervisor_pid,'--supervisor-creation-filetime',$resolutionLease.supervisor_creation_filetime,
        '--session-token',$resolutionLease.session_token,
        '--width',[string]$renderPlan.width,'--height',[string]$renderPlan.height,'--report',$resolutionReport)
    $resolutionAttempted=$true
    $resolutionProcess=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2NativeResolution.exe') $resolutionArguments $folder 'resolution'
    $state.resolution_helper=Get-Bc2ProcessIdentity $resolutionProcess;Save-State
    # This helper may be inside an original engine call. Its bounded worker
    # restores/drains the temporary resize hook; the session guard remains armed.
    # Never kill the helper on a stop request.
    if(-not $resolutionProcess.WaitForExit(32000)){
        $state.resolution_cleanup_pending=$true
        throw 'Native resolution setup has not finished cleanup. No VR adapter was attached. Close BC2 before retrying.'
    }
    if($resolutionProcess.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $resolutionReport -PathType Leaf)){
        throw (Get-Bc2StartupFailure $folder 'resolution' 'BC2 could not configure its native render targets for the headset. No VR adapter was attached.')
    }
    $resolutionProof=Read-FvrJson $resolutionReport
    Assert-Bc2NativeResolution $renderPlan $resolutionProof $resolutionLease
    $state.resolution_guard_active=$true
    if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
    # Independently observe the original renderer again after the helper has
    # restored its hook. HWND sizing or a larger XR swapchain is not proof that
    # BC2 itself renders at the headset dimensions.
    & $options.python -B (Join-Path $PSScriptRoot 'tools/wait_main_menu.py') --pid $game.Id --creation-filetime $creation --exe $exe --expected-sha256 $options.game_sha256 --output (Join-Path $folder 'menu-resized.json') @menuCompatibilityArguments *> (Join-Path $folder 'menu-resized.log')
    if($LASTEXITCODE){throw (Get-Bc2StartupFailure $folder 'menu-resized' 'The resized BC2 main menu did not become ready. No VR adapter was attached.')}
    $menuReady=Read-FvrJson (Join-Path $folder 'menu-resized.json')
    if($menuReady.passed -isnot [bool] -or -not $menuReady.passed -or
       (Get-Bc2ResolutionInteger $menuReady 'width' 16384) -ne $renderPlan.width -or
       (Get-Bc2ResolutionInteger $menuReady 'height' 16384) -ne $renderPlan.height){
        throw 'The live BC2 renderer does not match the headset render plan. No VR adapter was attached.'
    }
    $state.width=$menuReady.width;$state.height=$menuReady.height;$state.native_resolution_verified=$true;Save-State
    if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
    if($options.activate_window){
        $game.Refresh()
        & $options.python -B (Join-Path $PSScriptRoot 'tools/activate_game_once.py') --pid $game.Id --creation-filetime $creation --exe $exe --expected-sha256 $options.game_sha256 --hwnd $game.MainWindowHandle.ToInt64() --output (Join-Path $folder 'activation.json') --allow-initial-activation *> (Join-Path $folder 'activation.log')
        # Windows may refuse foreground activation while another application
        # owns input. The native menu channel works without desktop focus.
        # Record the one attempt; never steal focus repeatedly or restart the
        # game solely because activation was refused.
        if($LASTEXITCODE){$state.activation_warning='Windows refused the optional foreground request; continuing with the exact original game.';Save-State}
        if(-not(Find-Bc2OwnedProcess $state.game)){
            $state.activation_process_outcome=Get-Bc2ActivationProcessOutcome $game
            if($state.activation_process_outcome.exit_observed){
                $detail=if($null -ne $state.activation_process_outcome.exit_code){'process exit code '+$state.activation_process_outcome.exit_code}else{'process exit code unavailable'}
                $state.error="Original BC2 exited during window activation ($detail). No VR adapter was attached. Use Report feedback to save diagnostics."
            }else{
                $state.error='Unable to verify the original BC2 process after window activation. No VR adapter was attached. Use Report feedback to save diagnostics.'
            }
            # Preserve this bounded observation even if later cleanup cannot
            # access the retained process. Failed ownership never admits VR.
            $state.state='failed';Save-State
            throw $state.error
        }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/BC2NativeProbe.dll') -Destination (Join-Path $folder 'BC2NativeProbe.dll')
    Copy-Item -LiteralPath (Join-Path $options.cache 'body-ammo-assets.fvrprop') -Destination (Join-Path $folder 'body-ammo-assets.fvrprop')
    $channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json'
    $state.phase='Connecting the headset through '+$options.xr_label+'…';Save-State
    $hostArgs=@('--ipc','--until-game-exit',[string]$game.Id,'--request-lifetime','150','--eye-width',[string]$menuReady.width,'--eye-height',[string]$menuReady.height,'--channel-file',$channel,'--ready-file',$ready,'--stop-file',$stopFile,'--loader',(Join-Path $PSScriptRoot 'bin/openxr_loader.dll'),'--body-props-cache',(Join-Path $options.cache 'body-ammo-assets.fvrprop'),'--controllers','--roomscale','--world-units-per-meter','1')
    $hostProcess=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2XrHost.exe') $hostArgs $folder 'host' @{XR_RUNTIME_JSON=$options.xr_manifest}
    $state.host=Get-Bc2ProcessIdentity $hostProcess;Save-State
    $deadline=[DateTime]::UtcNow.AddSeconds(30);$requirements=$null
    while([DateTime]::UtcNow -lt $deadline -and -not $hostProcess.HasExited){
        if(Test-Path -LiteralPath $ready){try{$candidate=Read-FvrJson $ready;if($candidate.ContainsKey('session_created') -and $candidate.ContainsKey('width') -and $candidate.ContainsKey('height')){$requirements=$candidate;break}}catch{}}
        if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
        Start-Sleep -Milliseconds 100
    }
    if(-not $requirements -or -not $requirements.session_created -or $requirements.width -ne $menuReady.width -or $requirements.height -ne $menuReady.height -or
       -not $requirements.ContainsKey('recommended_eye_width') -or -not $requirements.ContainsKey('recommended_eye_height') -or
       $requirements.recommended_eye_width -ne $renderPlan.width -or $requirements.recommended_eye_height -ne $renderPlan.height){
        throw (Get-Bc2StartupFailure $folder 'host' ('OpenXR session did not become ready at the verified headset resolution using '+$options.xr_label+'. If its render-resolution setting changed during startup, restart BC2 from the launcher.'))
    }
    if($game.HasExited -or -not(Find-Bc2OwnedProcess $state.game)){throw 'Original BC2 game exited before attach.'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim();if(-not $token){throw 'Missing VR channel.'}
    $nativeArguments=@('--game',$options.game_path,'--dll',(Join-Path $folder 'BC2NativeProbe.dll'),'--report',(Join-Path $folder 'native-trace.json'),'--stream',$token,'--controllers','--motion-aim','--body-follow','--pose-observe','--hand-poses','--muzzle-fire','--two-hand-grip','--sight-flip','--physical-reload','--magazine-reload','--body-inventory','--boat-head-aim','--boat-head-fire','--uncap-mirror','--until-host-exit',[string]$hostProcess.Id)
    $trace=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2NativeTrace.exe') $nativeArguments $folder 'native'
    $state.trace=Get-Bc2ProcessIdentity $trace;$state.phase='Attaching VR and waiting for the native connection. Keep the headset connected…';Save-State
    $attachDeadline=[DateTime]::UtcNow.AddSeconds(35);$attached=$false
    while([DateTime]::UtcNow -lt $attachDeadline){
        if($trace.HasExited){
            $outcome=Save-NativeOutcome
            if($outcome -and $outcome.outcome -ceq 'game_exited'){throw 'BC2 exited during VR startup; the native probe return was not observed. Use Report feedback to save diagnostics.'}
            throw (Get-Bc2StartupFailure $folder 'native' 'The native VR adapter exited during startup.')
        }
        if($hostProcess.HasExited){throw (Get-Bc2StartupFailure $folder 'host' 'The headset runtime exited during startup.')}
        if($game.HasExited){throw 'BC2 exited during VR startup.'}
        if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
        if(Get-Bc2NativeStartupSample (Join-Path $folder 'session-telemetry.jsonl')){$attached=$true;break}
        Start-Sleep -Milliseconds 200
    }
    if(-not $attached){throw 'The native VR input connection did not become ready. Keep the headset awake and connected, then retry. Use Report feedback to save the native and headset logs.'}
    $state.state='running';$state.phase='VR connected; native VR input received.';Save-State
    $nextSpace=[DateTime]::UtcNow.AddSeconds(10)
    while(-not $trace.WaitForExit(250)){
        if([DateTime]::UtcNow -ge $nextSpace){
            $used=[long](Get-ChildItem -LiteralPath $folder -File -Recurse|Measure-Object Length -Sum).Sum
            if($used -gt 768MB){throw 'Session evidence exceeded its 768 MiB storage budget; VR is stopping.'}
            Assert-FvrFreeSpace $options.state_root 64MB;$nextSpace=[DateTime]::UtcNow.AddSeconds(10)
        }
    }
    $state.native_exit_code=$trace.ExitCode
    $outcome=Save-NativeOutcome
    # Observed remote errors keep their existing failure path. A signaled game
    # has no remote result; an expected close is not a new adapter failure.
    if($trace.ExitCode -ne 0){throw (Get-Bc2StartupFailure $folder 'native' "Native VR session exited with code $($trace.ExitCode).")}
    if($outcome -and $outcome.outcome -ceq 'game_exited'){
        if(-not $game.HasExited){throw 'Native diagnostics reported a game exit without the original BC2 process exiting.'}
        if($null -ne $outcome.game_exit_code -and $outcome.game_exit_code -ne 0){
            throw "BC2 exited with process code $($outcome.game_exit_code) before the native probe return was observed. Use Report feedback to save diagnostics."
        }
        $state.phase='BC2 closed; native cleanup and final report completion were not observed.'
    }
    $state.state='ended'
} catch {$state.state='failed';$state.error=$_.Exception.Message;Write-Error -Message $_.Exception.Message -ErrorAction Continue}
finally {
    try {Stop-OwnedHost}catch{$state.state='failed';$state.error+=" Headset host cleanup failed: $($_.Exception.Message)"}
    # Native bootstrap must drain its hooks after the host exits. Do not kill a
    # bootstrap attached to a live game; retain exact ownership on a slow exit.
    if($trace -and -not $trace.HasExited){$state.native_drained=$trace.WaitForExit(15000);if(-not $state.native_drained){$state.error+=' Native cleanup is still pending. Close BC2 before another launch.';$state.state='failed'}}
    $null=Save-NativeOutcome
    if($trace -and $trace.HasExited -and $trace.ExitCode -ne 0 -and $state.state -ne 'failed'){
        $state.state='failed';$state.error=Get-Bc2StartupFailure $folder 'native' "Native VR session exited with code $($trace.ExitCode)."
    }
    try {Complete-ResolutionSession}catch{
        $state.resolution_cleanup_pending=$true;$state.resolution_cleanup='unconfirmed'
        $state.state='failed'
        if($state.ContainsKey('activation_process_outcome')){
            $state.error+=' Resolution cleanup remains unconfirmed because the original BC2 process could not be verified.'
        }else{$state.error+=" $($_.Exception.Message)"}
    }
    if(-not $state.ContainsKey('activation_process_outcome') -and $state.state -eq 'failed' -and $game -and -not $game.HasExited -and $state.game -and (Find-Bc2OwnedProcess $state.game)){
        $state.restart_required=$true
        $state.error+=' BC2 remains open. Enable Restart BC2 before launch, or close BC2 normally and launch again. Reinstalling BC2VR is unnecessary.'
    }
    $state.ended_utc=[DateTime]::UtcNow.ToString('o');Save-State
    # A stop request can return from inside try. The failure exit belongs here
    # so that return cannot conceal a cleanup failure recorded by finally.
    if($state.state -eq 'failed'){exit 1}
}

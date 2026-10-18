#requires -Version 7.5
param([Parameter(Mandatory)][string]$OptionsPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../app/Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Runtime.Core.ps1')
$options=Read-FvrJson $OptionsPath
$folder=Get-FvrChild $options.state_root $options.relative_folder
Assert-FvrNoLinks $folder
if([IO.Path]::GetFullPath($OptionsPath) -ne (Join-Path $folder 'options.json')){throw 'Session options escaped the owned folder.'}
$null=Set-FvrLauncherStorage (Split-Path (Split-Path $options.state_root -Parent) -Parent)
$state=@{schema='bc2vr.session.v1';state='starting';version=$options.version;source_sha256=$options.source_sha256;started_utc=[DateTime]::UtcNow.ToString('o');game=$null;host=$null;trace=$null;error=$null;supervisor=(Get-Bc2ProcessIdentity (Get-Process -Id $PID))}
$stateFile=Join-Path $folder 'session.json';$stopFile=Join-Path $folder 'stop.request'
$hostProcess=$null;$game=$null;$trace=$null
function Save-State {Write-FvrJson $stateFile $state}
function Stop-OwnedHost {
    if($hostProcess -and -not $hostProcess.HasExited){
        [IO.File]::WriteAllText($stopFile,'Session supervisor cleanup')
        if(-not $hostProcess.WaitForExit(10000)){$hostProcess.Kill();$null=$hostProcess.WaitForExit(2000);$state.host_forced_stop=$true}
    }
}
try {
    Save-State
    $config=Get-Bc2RuntimeConfig
    if($options.source_sha256 -ne $config.source_sha256 -or $options.version -ne $config.version){throw 'Runtime changed between launch and session.'}
    Assert-Bc2RuntimeSpace $options.state_root
    $exe=Join-Path $options.game_path 'BFBC2Game.exe'
    if((Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant() -ne $options.game_sha256 -or $options.game_sha256 -notin $config.game_sha256){throw 'BC2 executable changed before launch.'}
    if(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'BC2 or a VR helper started concurrently; launch cancelled.'}
    if(-not(Test-Bc2AssetCache $options.cache $config.asset_recipe_sha256 $options.game_sha256)){throw 'Prepared assets changed before launch.'}
    # Audio settings are attached only to this new game process, never to the
    # machine/user environment or a guessed already-running game.
    $game=Start-Process -FilePath $exe -WorkingDirectory $options.game_path -Environment (Get-Bc2AudioEnvironment $options.cache) -PassThru
    $state.game=Get-Bc2ProcessIdentity $game;$state.audio_environment_inherited=$true;Save-State
    $creation=$game.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
    & $options.python -B (Join-Path $PSScriptRoot 'tools/wait_main_menu.py') --pid $game.Id --creation-filetime $creation --exe $exe --expected-sha256 $options.game_sha256 --output (Join-Path $folder 'menu-ready.json') *> (Join-Path $folder 'menu-ready.log')
    if($LASTEXITCODE){throw 'BC2 main menu did not become ready. See menu-ready.log; the game remains open.'}
    if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
    if($options.activate_window){
        $game.Refresh()
        & $options.python -B (Join-Path $PSScriptRoot 'tools/activate_game_once.py') --pid $game.Id --creation-filetime $creation --exe $exe --expected-sha256 $options.game_sha256 --hwnd $game.MainWindowHandle.ToInt64() --output (Join-Path $folder 'activation.json') --allow-initial-activation *> (Join-Path $folder 'activation.log')
        # Windows may refuse foreground activation while another application
        # owns input. The native menu channel works without desktop focus.
        # Record the one attempt; never steal focus repeatedly or restart the
        # game solely because activation was refused.
        if($LASTEXITCODE){$state.activation_warning='Windows refused the optional foreground request; continuing with the exact original game.';Save-State}
        if(-not(Find-Bc2OwnedProcess $state.game)){throw 'Original BC2 exited during window activation.'}
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/BC2NativeProbe.dll') -Destination (Join-Path $folder 'BC2NativeProbe.dll')
    Copy-Item -LiteralPath (Join-Path $options.cache 'body-ammo-assets.fvrprop') -Destination (Join-Path $folder 'body-ammo-assets.fvrprop')
    $channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json'
    $hostArgs=@('--ipc','--until-game-exit',[string]$game.Id,'--request-lifetime','150','--eye-width','1920','--eye-height','1080','--channel-file',$channel,'--ready-file',$ready,'--stop-file',$stopFile,'--loader',(Join-Path $PSScriptRoot 'bin/openxr_loader.dll'),'--body-props-cache',(Join-Path $options.cache 'body-ammo-assets.fvrprop'),'--controllers','--roomscale','--world-units-per-meter','1')
    $hostProcess=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2XrHost.exe') $hostArgs $folder 'host'
    $state.host=Get-Bc2ProcessIdentity $hostProcess;Save-State
    $deadline=[DateTime]::UtcNow.AddSeconds(30);$requirements=$null
    while([DateTime]::UtcNow -lt $deadline -and -not $hostProcess.HasExited){
        if(Test-Path -LiteralPath $ready){try{$candidate=Read-FvrJson $ready;if($candidate.ContainsKey('session_created') -and $candidate.ContainsKey('width') -and $candidate.ContainsKey('height')){$requirements=$candidate;break}}catch{}}
        if(Test-Path -LiteralPath $stopFile){$state.state='stopped';return}
        Start-Sleep -Milliseconds 100
    }
    if(-not $requirements -or -not $requirements.session_created -or $requirements.width -ne 1920 -or $requirements.height -ne 1080){throw 'OpenXR did not become ready. Connect the headset to SteamVR and try again.'}
    if($game.HasExited -or -not(Find-Bc2OwnedProcess $state.game)){throw 'Original BC2 game exited before attach.'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim();if(-not $token){throw 'Missing VR channel.'}
    $nativeArguments=@('--game',$options.game_path,'--dll',(Join-Path $folder 'BC2NativeProbe.dll'),'--report',(Join-Path $folder 'native-trace.json'),'--stream',$token,'--controllers','--motion-aim','--body-follow','--pose-observe','--hand-poses','--muzzle-fire','--two-hand-grip','--sight-flip','--physical-reload','--magazine-reload','--body-inventory','--boat-head-aim','--boat-head-fire','--uncap-mirror','--until-host-exit',[string]$hostProcess.Id)
    $trace=Start-Bc2Child (Join-Path $PSScriptRoot 'bin/BC2NativeTrace.exe') $nativeArguments $folder 'native'
    $state.trace=Get-Bc2ProcessIdentity $trace;$state.state='running';Save-State
    $nextSpace=[DateTime]::UtcNow.AddSeconds(10)
    while(-not $trace.WaitForExit(250)){
        if([DateTime]::UtcNow -ge $nextSpace){
            $used=[long](Get-ChildItem -LiteralPath $folder -File -Recurse|Measure-Object Length -Sum).Sum
            if($used -gt 768MB){throw 'Session evidence exceeded its 768 MiB storage budget; VR is stopping.'}
            Assert-FvrFreeSpace $options.state_root 64MB;$nextSpace=[DateTime]::UtcNow.AddSeconds(10)
        }
    }
    $state.native_exit_code=$trace.ExitCode
    if($trace.ExitCode -ne 0 -and -not $game.HasExited){throw "Native VR session exited with code $($trace.ExitCode). See native.stderr.log."}
    $state.state='ended'
} catch {$state.state='failed';$state.error=$_.Exception.Message;Write-Error -Message $_.Exception.Message -ErrorAction Continue}
finally {
    Stop-OwnedHost
    # Native bootstrap must drain its hooks after the host exits. Do not kill a
    # bootstrap attached to a live game; retain exact ownership on a slow exit.
    if($trace -and -not $trace.HasExited){$state.native_drained=$trace.WaitForExit(15000);if(-not $state.native_drained){$state.error='Native cleanup is still pending. Close BC2 before another launch.';$state.state='failed'}}
    $state.ended_utc=[DateTime]::UtcNow.ToString('o');Save-State
}
if($state.state -eq 'failed'){exit 1}

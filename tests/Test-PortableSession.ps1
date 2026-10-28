#requires -Version 7.5
# Run the real session coordinator in child PowerShell processes. The fixture
# runtime replaces all game/XR/native process operations; no game is accessed.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$root=Join-Path $repo ('test-temp/session-start-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0;$passed=$false
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
try {
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $root -Operation Session -PlannedBytes 2MB -RawCaptureBytes 0
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage admission required'}
    $null=Set-FvrLauncherStorage $root
    $app=Join-Path $root 'package/app';$runtime=Join-Path $root 'package/runtime';$bin=Join-Path $runtime 'bin'
    foreach($p in @($app,$runtime,$bin)){[IO.Directory]::CreateDirectory($p)|Out-Null}
    foreach($name in @('Launcher.Core.ps1','Launcher.Storage.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination $app}
    foreach($name in @('Session.ps1','Startup.ps1','Resolution.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/runtime/'+$name)) -Destination $runtime}
    [IO.File]::WriteAllText((Join-Path $bin 'BC2NativeProbe.dll'),'fixture data; never loaded')
    [IO.File]::WriteAllText((Join-Path $bin 'BC2ResolutionBootstrap.dll'),'fixture resolution data; never loaded')
    $fake=@'
$script:activationUnverified=$false
function Assert-Bc2RuntimeSpace($Root){}
function Get-Bc2RuntimeConfig {return Read-FvrJson (Join-Path $PSScriptRoot 'fixture.json')}
function Test-Bc2AssetCache($Cache,$Recipe,$Hash){return $true}
function Get-Bc2AudioEnvironment($Cache){return @{fixture='true'}}
function Get-Bc2ProcessIdentity($Process){return @{pid=$Process.Id;start_utc='2026-10-18T00:00:00Z';path=$Process.Path}}
function Find-Bc2OwnedProcess($Identity){if($script:activationUnverified){return $null};return $script:fakeGame}
function Get-Process {param($Name,$Id,$ErrorAction) if($Id){return [pscustomobject]@{Id=$Id;Path='fixture-supervisor'}}}
function New-FakeProcess($Id,$Name,$Exited=$false){
    $p=[pscustomobject]@{Id=$Id;Path=$Name;HasExited=$Exited;ExitCode=0;StartTime=[DateTime]'2026-10-18T00:00:00Z';MainWindowHandle=[IntPtr]123}
    $p|Add-Member ScriptMethod Refresh {}
    $p|Add-Member ScriptMethod WaitForExit {
        param($Timeout)
        if($this.Path -eq 'fake-host'){
            if(-not(Test-Path -LiteralPath (Join-Path $folder 'stop.request'))){throw 'Host stop did not request cleanup'}
            [IO.File]::AppendAllText((Join-Path $folder 'boundary-order.txt'),"host-stopped`n")
        }
        $this.HasExited=$true;return $true
    }
    return $p
}
function Start-Process {param($FilePath,$WorkingDirectory,$Environment,[switch]$PassThru)
    [IO.File]::WriteAllText((Join-Path $folder 'game-created.txt'),'fixture only')
    $script:fakeGame=New-FakeProcess 100 'BFBC2Game.exe';return $script:fakeGame
}
function Invoke-FixturePython {
    $out=$args[[Array]::IndexOf($args,'--output')+1]
    $cfg=Get-Bc2RuntimeConfig
    if([IO.Path]::GetFileName($out) -eq 'activation.json'){
        [IO.File]::AppendAllText((Join-Path $folder 'boundary-order.txt'),"activation`n")
        $script:activationUnverified=$cfg.scenario -ne 'activation_success'
        if($cfg.scenario -in @('activation_exit','activation_exit_zero','activation_exit_unknown')){$script:fakeGame.HasExited=$true}
        if($cfg.scenario -eq 'activation_exit'){$script:fakeGame.ExitCode=[int]-1073741819}
        if($cfg.scenario -eq 'activation_exit_unknown'){$script:fakeGame|Add-Member ScriptProperty ExitCode {throw 'private activation fixture detail'} -Force}
        if($cfg.scenario -eq 'activation_access_unavailable'){$script:fakeGame|Add-Member ScriptProperty HasExited {throw 'private activation fixture detail'} -Force}
        Write-FvrJson $out @{passed=$true;fixture=$true}
        $global:LASTEXITCODE=0;return
    }
    $resized=[IO.Path]::GetFileName($out) -eq 'menu-resized.json'
    [IO.File]::AppendAllText((Join-Path $folder 'boundary-order.txt'),$(if($resized){'resized_menu'}else{'initial_menu'})+"`n")
    if($resized -and -not(Test-Path -LiteralPath (Join-Path $folder 'resolution-native.json'))){throw 'Renderer readback preceded resolution setup'}
    $changed=$resized -and $cfg.scenario -ne 'renderer_did_not_resize'
    Write-FvrJson $out @{passed=($cfg.scenario -ne 'wrong_renderer');width=$(if($changed){2784}else{1920});height=$(if($changed){3024}else{1080});error='Direct3D 11 required; set DxVersion=11.'}
    $global:LASTEXITCODE=if($cfg.scenario -eq 'wrong_renderer'){1}else{0}
}
function Start-Bc2Child($File,$Arguments,$Folder,$LogName,$Environment=@{}){
    $cfg=Get-Bc2RuntimeConfig
    [IO.File]::AppendAllText((Join-Path $Folder 'boundary-order.txt'),$LogName+"`n")
    if($LogName -eq 'resolution'){
        if([IO.Path]::GetFileName($File) -ne 'BC2NativeResolution.exe'){throw 'Wrong resolution helper'}
        Write-FvrJson (Join-Path $Folder 'resolution-arguments.json') @{arguments=$Arguments}
        $lease=Read-FvrJson (Join-Path $Folder 'resolution-lease.json')
        foreach($pair in @{'--operation'='configure';'--pid'=[string]$lease.pid;'--creation-filetime'=$lease.creation_filetime;'--dll-sha256'=$lease.dll_sha256;'--supervisor-pid'=[string]$lease.supervisor_pid;'--supervisor-creation-filetime'=$lease.supervisor_creation_filetime;'--session-token'=$lease.session_token}.GetEnumerator()){
            $i=[Array]::IndexOf($Arguments,$pair.Key);if($i -lt 0 -or $Arguments[$i+1] -cne $pair.Value){throw "Unbound configure argument $($pair.Key)"}
        }
        $proof=$lease.Clone();$proof.schema='bc2vr.native-resolution.v2';$proof.operation='configure'
        foreach($pair in @{passed=$true;applied=$true;hook_restored=$true;callbacks_drained=$true;requested_width=2784;requested_height=3024;width=2784;height=3024;updates=4;ui_thread=700;guard_installed=$true;guard_active=$true;guard_disabled=$false;guard_drained=$false;guard_released=$false;guard_owner_lost=$false;guard_cleanup_pending=$false;guard_never_installed=$false}.GetEnumerator()){$proof[$pair.Key]=$pair.Value}
        if($cfg.scenario -eq 'resolution_failure'){$proof.passed=$false}
        if($cfg.scenario -eq 'resolution_hook_live'){$proof.hook_restored=$false}
        if($cfg.scenario -eq 'resolution_callback_live'){$proof.callbacks_drained=$false}
        if($cfg.scenario -eq 'wrong_native_size'){$proof.width=1920}
        if($cfg.scenario -eq 'wrong_native_pid'){$proof.pid=101}
        if($cfg.scenario -eq 'wrong_guard_token'){$proof.session_token='f'*32}
        if($cfg.scenario -eq 'wrong_guard_supervisor'){$proof.supervisor_pid++}
        if($cfg.scenario -eq 'stale_guard_game'){$proof.creation_filetime='1'}
        if($cfg.scenario -eq 'stale_guard_supervisor'){$proof.supervisor_creation_filetime='1'}
        if($cfg.scenario -eq 'guard_not_armed'){$proof.guard_active=$false}
        if($cfg.scenario -eq 'missing_guard_proof'){$proof.Remove('guard_active')}
        if($cfg.scenario -eq 'never_installed'){$proof.passed=$false;$proof.guard_installed=$false;$proof.guard_active=$false}
        $out=$Arguments[[Array]::IndexOf($Arguments,'--report')+1]
        if($cfg.scenario -ne 'missing_setup_report'){Write-FvrJson $out $proof}
        $p=New-FakeProcess 150 'fake-resolution' $true
        if($cfg.scenario -in @('resolution_failure','never_installed')){$p.ExitCode=1}
        if($cfg.scenario -eq 'resolution_timeout'){
            $p.HasExited=$false
            $p|Add-Member ScriptMethod WaitForExit {param($Timeout)return $false} -Force
            $p|Add-Member ScriptMethod Kill {throw 'Cannot terminate a helper inside original engine calls'}
        }
        if($cfg.scenario -eq 'stop_during_resolution'){[IO.File]::WriteAllText((Join-Path $Folder 'stop.request'),'fixture stop')}
        return $p
    }
    if($LogName -eq 'resolution-release'){
        $lease=Read-FvrJson (Join-Path $Folder 'resolution-lease.json')
        foreach($pair in @{'--operation'='release';'--pid'=[string]$lease.pid;'--creation-filetime'=$lease.creation_filetime;'--dll-sha256'=$lease.dll_sha256;'--supervisor-pid'=[string]$lease.supervisor_pid;'--supervisor-creation-filetime'=$lease.supervisor_creation_filetime;'--session-token'=$lease.session_token}.GetEnumerator()){
            $i=[Array]::IndexOf($Arguments,$pair.Key);if($i -lt 0 -or $Arguments[$i+1] -cne $pair.Value){throw "Unbound release argument $($pair.Key)"}
        }
        if($cfg.scenario -in @('resolution_timeout','native_cleanup_timeout','native_exception_game_live')){throw 'Release must not race unresolved native cleanup'}
        Write-FvrJson (Join-Path $Folder 'release-arguments.json') @{arguments=$Arguments}
        $proof=$lease.Clone();$proof.schema='bc2vr.native-resolution.v2';$proof.operation='release'
        foreach($pair in @{passed=$true;hook_restored=$true;callbacks_drained=$true;guard_installed=$true;guard_active=$false;guard_disabled=$true;guard_drained=$true;guard_released=$true;guard_never_installed=$false;guard_cleanup_pending=$false}.GetEnumerator()){$proof[$pair.Key]=$pair.Value}
        if($cfg.scenario -eq 'wrong_release_token'){$proof.session_token='e'*32}
        if($cfg.scenario -eq 'release_not_drained'){$proof.guard_drained=$false;$proof.guard_cleanup_pending=$true}
        if($cfg.scenario -eq 'never_installed'){$proof.guard_released=$false;$proof.guard_never_installed=$true}
        if($cfg.scenario -eq 'game_exits_during_release'){$script:fakeGame.HasExited=$true;$proof.game_exited=$true;$proof.guard_released=$false}
        if($cfg.scenario -eq 'false_game_exited'){$proof.game_exited=$true;$proof.guard_released=$false}
        $out=$Arguments[[Array]::IndexOf($Arguments,'--report')+1]
        if($cfg.scenario -ne 'missing_release_report'){Write-FvrJson $out $proof}
        $p=New-FakeProcess 160 'fake-resolution-release' $true
        if($cfg.scenario -eq 'release_timeout'){$p.HasExited=$false;$p|Add-Member ScriptMethod WaitForExit {param($Timeout)return $false} -Force;$p|Add-Member ScriptMethod Kill {throw 'Cannot kill a draining release helper'}}
        return $p
    }
    if($LogName -eq 'host'){
        Write-FvrJson (Join-Path $Folder 'host-arguments.json') @{arguments=$Arguments;environment=$Environment}
        $w=[int]$Arguments[[Array]::IndexOf($Arguments,'--eye-width')+1];$h=[int]$Arguments[[Array]::IndexOf($Arguments,'--eye-height')+1]
        Write-FvrJson (Join-Path $Folder 'ready.json') @{session_created=$true;width=$(if($cfg.scenario -eq 'bad_host_size'){1920}else{$w});height=$h;recommended_eye_width=$(if($cfg.scenario -eq 'changed_xr_recommendation'){3000}else{$w});recommended_eye_height=$h}
        [IO.File]::WriteAllText((Join-Path $Folder 'channel.txt'),'fixture-channel')
        return (New-FakeProcess 200 'fake-host')
    }
    if($LogName -ne 'native'){throw 'Unexpected child; this fixture never starts real helpers.'}
    if($cfg.scenario -eq 'native_failure'){
        [IO.File]::WriteAllText((Join-Path $Folder 'native.stderr.log'),'Fixture native profile mismatch')
        return (New-FakeProcess 300 'fake-native' $true)
    }
    [IO.File]::WriteAllText((Join-Path $Folder 'session-telemetry.jsonl'),"{`"input_samples`":10,`"gpu_failure_stage`":0,`"camera_restore_failures`":0}`n")
    $native=New-FakeProcess 300 'fake-native'
    $native|Add-Member NoteProperty FixtureScenario $cfg.scenario
    if($cfg.scenario -in @('stop_after_attach','stop_after_attach_game_fault','native_cleanup_timeout')){[IO.File]::WriteAllText((Join-Path $Folder 'stop.request'),'fixture stop')}
    $native|Add-Member ScriptMethod WaitForExit {
        param($Timeout)
        if($this.FixtureScenario -in @('stop_after_attach','stop_after_attach_game_fault','native_cleanup_timeout')){
            $order=Get-Content -LiteralPath (Join-Path $folder 'boundary-order.txt')
            if('host-stopped' -notin $order){throw 'Native cleanup must follow host stop'}
            if($this.FixtureScenario -eq 'native_cleanup_timeout'){return $false}
        }
        $this.HasExited=$true
        [IO.File]::AppendAllText((Join-Path $folder 'boundary-order.txt'),"native-exited`n")
        if($this.FixtureScenario -in @('normal_game_close','game_close_exit_unknown','game_process_fault','stop_after_attach_game_fault','native_exception_game_gone')){$script:fakeGame.HasExited=$true}
        if($this.FixtureScenario -in @('native_exception_game_gone','native_exception_game_live')){
            $this.ExitCode=2
            [IO.File]::WriteAllText((Join-Path $folder 'native.stdout.log'),'{"pid":100,"probe_exit":3221225477,"module_retained_until_game_exit":true,"game_files_modified":false}')
        }
        if($this.FixtureScenario -notlike 'native_exception_*'){
            $gone=$this.FixtureScenario -in @('normal_game_close','game_close_exit_unknown','game_process_fault','stop_after_attach_game_fault','native_game_exit_live')
            $processCode=if($this.FixtureScenario -in @('game_process_fault','stop_after_attach_game_fault')){3221225477}elseif($this.FixtureScenario -eq 'game_close_exit_unknown'){$null}else{0}
            Write-FvrJson (Join-Path $folder 'native.stdout.log') @{
                schema='bc2vr.native-run-outcome.v1';pid=100
                outcome=$(if($gone){'game_exited'}else{'remote_thread_exited'})
                remote_thread_exit_observed=(-not $gone);probe_exit=$(if($gone){$null}else{0})
                game_exit_code=$(if($gone){$processCode}else{$null});report_bytes=0;report_nonempty=$false
            }
        }
        return $true
    } -Force
    return $native
}
function Write-FvrJson($Path,$Value){
    $text=$Value|ConvertTo-Json -Depth 30
    [IO.File]::WriteAllText($Path,$text)
    if([IO.Path]::GetFileName($Path) -eq 'session.json'){
        [IO.File]::AppendAllText((Join-Path (Split-Path $Path -Parent) 'state-history.txt'),$Value.state+"`n")
    }
}
'@
    [IO.File]::WriteAllText((Join-Path $runtime 'Runtime.Core.ps1'),$fake)
    foreach($scenario in @('success','activation_success','activation_exit','activation_exit_zero','activation_exit_unknown','activation_live_unverified','activation_access_unavailable',
        'native_failure','bad_host_size','wrong_renderer','normal_game_close','game_close_exit_unknown','game_process_fault','stop_after_attach_game_fault','native_game_exit_live','native_exception_game_gone','native_exception_game_live',
        'invalid_plan','resolution_failure','resolution_hook_live','resolution_callback_live','resolution_timeout','wrong_native_size','wrong_native_pid','renderer_did_not_resize','changed_xr_recommendation','stop_during_resolution',
        'missing_setup_report','wrong_guard_token','wrong_guard_supervisor','stale_guard_game','stale_guard_supervisor','guard_not_armed','missing_guard_proof',
        'stop_after_attach','native_cleanup_timeout','wrong_release_token','release_not_drained','release_timeout','missing_release_report','never_installed','game_exits_during_release','false_game_exited')){
        $install=Join-Path $root $scenario;$stateRoot=Join-Path $install 'data/runtime';$run=Join-Path $stateRoot 'sessions/current'
        $game=Join-Path $install 'game';$cache=Join-Path $stateRoot 'cache'
        foreach($p in @($run,$game,$cache)){[IO.Directory]::CreateDirectory($p)|Out-Null}
        $exe=Join-Path $game 'BFBC2Game.exe';[IO.File]::WriteAllText($exe,'fixture data; never executed')
        [IO.File]::WriteAllText((Join-Path $cache 'body-ammo-assets.fvrprop'),'fixture data')
        $hash=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
        Write-FvrJson (Join-Path $runtime 'fixture.json') @{scenario=$scenario;version='0.1.6';source_sha256=('a'*64);game_sha256=@($hash);asset_recipe_sha256=('b'*64)}
        $options=Join-Path $run 'options.json'
        $plan=@{schema='bc2vr.render-plan.v1';width=2784;height=3024;recommended_width=2784;recommended_height=3024;max_width=8192;max_height=8192;source='openxr_recommendation';native_source_verified=$false}
        if($scenario -eq 'invalid_plan'){$plan.width=1920}
        Write-FvrJson $options @{state_root=$stateRoot;relative_folder='sessions/current';game_path=$game;game_sha256=$hash;cache=$cache;python='Invoke-FixturePython';activate_window=($scenario -like 'activation_*');version='0.1.6';source_sha256=('a'*64);xr_manifest='fixture-openxr.json';xr_label='Virtual Desktop fixture';render_plan=$plan}
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File (Join-Path $runtime 'Session.ps1') -OptionsPath $options *> (Join-Path $run 'coordinator.log')
        $code=$LASTEXITCODE
        $state=Read-FvrJson (Join-Path $run 'session.json');$history=Get-Content -LiteralPath (Join-Path $run 'state-history.txt')
        if($scenario -in @('success','activation_success')){
            Check ($code -eq 0 -and $state.state -eq 'ended' -and 'running' -in $history) 'actual session coordinator requires native input then completes cleanly'
            $hostCall=Read-FvrJson (Join-Path $run 'host-arguments.json')
            $w=[Array]::IndexOf($hostCall.arguments,'--eye-width');$h=[Array]::IndexOf($hostCall.arguments,'--eye-height')
            Check ($hostCall.arguments[$w+1] -eq '2784' -and $hostCall.arguments[$h+1] -eq '3024') 'runtime-recommended tall dimensions reach host instead of initial desktop size'
            Check ($state.initial_width -eq 1920 -and $state.initial_height -eq 1080 -and $state.native_resolution_verified -eq $true) 'native resize is independently verified after helper completion'
            $order=Get-Content -LiteralPath (Join-Path $run 'boundary-order.txt')
            $expectedOrder='initial_menu,resolution,resized_menu,'+$(if($scenario -eq 'activation_success'){'activation,'}else{''})+'host,native,native-exited,host-stopped,resolution-release'
            Check (($order -join ',') -eq $expectedOrder) 'setup/readback and optional single activation precede attachment; release follows host/native cleanup'
            Check (-not $state.ContainsKey('activation_process_outcome')) 'verified original process does not receive a failed-ownership diagnosis'
            $resolutionCall=Read-FvrJson (Join-Path $run 'resolution-arguments.json')
            $pidIndex=[Array]::IndexOf($resolutionCall.arguments,'--pid');$createdIndex=[Array]::IndexOf($resolutionCall.arguments,'--creation-filetime')
            Check ($resolutionCall.arguments[$pidIndex+1] -eq '100' -and $resolutionCall.arguments[$createdIndex+1] -eq ([DateTime]'2026-10-18T00:00:00Z').ToUniversalTime().ToFileTimeUtc().ToString()) 'native helper binds the exact original game process and creation'
            Check ($hostCall.environment.XR_RUNTIME_JSON -eq 'fixture-openxr.json') 'session uses same selected OpenXR runtime as preflight'
            Check ($state.resolution_cleanup -eq 'released' -and $state.resolution_guard_released -eq $true -and $state.resolution_guard_active -eq $false) 'normal completion explicitly releases the exact session guard'
        }elseif($scenario -like 'activation_*'){
            $observation=$state.activation_process_outcome
            $order=Get-Content -LiteralPath (Join-Path $run 'boundary-order.txt')
            Check ($code -ne 0 -and $state.state -eq 'failed' -and 'running' -notin $history -and
                ($order -join ',') -eq 'initial_menu,resolution,resized_menu,activation') ($scenario+' rejects failed ownership after exactly one activation without attachment or cleanup calls into an unverified game')
            Check (-not(Test-Path -LiteralPath (Join-Path $run 'host-arguments.json')) -and
                -not(Test-Path -LiteralPath (Join-Path $run 'session-telemetry.jsonl')) -and
                $observation.schema -ceq 'bc2vr.activation-process.v1' -and
                $observation.ownership_verified -is [bool] -and -not $observation.ownership_verified) ($scenario+' saves bounded typed evidence before starting the headset host or native adapter')
            Check ($state.error.Contains('No VR adapter was attached.') -and $state.error.Contains('Report feedback') -and
                -not $state.error.Contains('crash',[StringComparison]::OrdinalIgnoreCase) -and
                ($state|ConvertTo-Json -Depth 20 -Compress) -notmatch 'private activation fixture detail') ($scenario+' preserves an accurate failure message without raw accessor exception details')
            Check ($state.ContainsKey('ended_utc') -and -not $state.ContainsKey('restart_required')) ($scenario+' reaches final save without re-reading an unverified process for restart advice')
            if($scenario -in @('activation_exit','activation_exit_zero','activation_exit_unknown')){
                Check ($observation.original_process_state -ceq 'exited' -and $observation.exit_observed -is [bool] -and
                    $observation.exit_observed -and $state.error.StartsWith('Original BC2 exited during window activation (')) ($scenario+' reports exit only when the retained original confirms it')
                if($scenario -eq 'activation_exit_unknown'){
                    Check ($null -eq $observation.exit_code -and $state.error.Contains('process exit code unavailable')) 'actual session retains an observed exit with unavailable code'
                }else{
                    $expectedCode=if($scenario -eq 'activation_exit'){[long]3221225477}else{[long]0}
                    Check (($observation.exit_code -is [long] -or $observation.exit_code -is [int]) -and
                        $observation.exit_code -eq $expectedCode -and $state.error.Contains("process exit code $expectedCode")) ($scenario+' retains the exact numeric process code including zero')
                }
            }else{
                $expectedState=if($scenario -eq 'activation_live_unverified'){'live'}else{'unavailable'}
                Check ($observation.original_process_state -ceq $expectedState -and -not $observation.exit_observed -and
                    $null -eq $observation.exit_code -and $state.error.StartsWith('Unable to verify the original BC2 process after window activation.') -and
                    -not $state.error.Contains('Original BC2 exited')) ($scenario+' never relabels unavailable ownership as process exit')
                Check ($state.resolution_cleanup -ceq 'unconfirmed' -and $state.resolution_cleanup_pending -eq $true) ($scenario+' records unknown guard cleanup without calling into the unverified process')
            }
        }elseif($scenario -eq 'stop_after_attach'){
            $order=Get-Content -LiteralPath (Join-Path $run 'boundary-order.txt')
            Check ($code -eq 0 -and $state.state -eq 'stopped' -and ($order[-3..-1] -join ',') -eq 'host-stopped,native-exited,resolution-release') 'early stop orders host shutdown then native drain then guard release'
        }elseif($scenario -eq 'stop_during_resolution'){
            Check ($code -eq 0 -and $state.state -eq 'stopped' -and -not(Test-Path -LiteralPath (Join-Path $run 'host-arguments.json'))) 'stop during helper waits for its restored result and never attaches VR'
        }elseif($scenario -in @('normal_game_close','game_close_exit_unknown')){
            Check ($code -eq 0 -and $state.state -eq 'ended' -and $state.native_exit_code -eq 0 -and -not $state.error -and 'running' -in $history) 'normal or unclassified game close after readiness remains a normal session end'
            Check ($state.resolution_cleanup -eq 'game_exited' -and -not(Test-Path -LiteralPath (Join-Path $run 'release-arguments.json'))) 'game exit removes guard lifetime without claiming an explicit release'
            Check ($state.native_outcome -eq 'game_exited' -and $null -eq $state.native_probe_exit -and -not $state.native_remote_thread_exit_observed -and -not $state.native_drained -and $state.native_cleanup -eq 'not_observed_game_exit') 'game exit does not fabricate a probe return or hook drain'
            Check ($state.native_report_bytes -eq 0 -and $state.native_report_nonempty -eq $false) 'empty final report remains explicit diagnostic evidence'
            if($scenario -eq 'game_close_exit_unknown'){Check ($null -eq $state.native_game_exit_code) 'missing process exit code is not converted to success0'}
        }elseif($scenario -in @('game_process_fault','stop_after_attach_game_fault')){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.native_game_exit_code -eq 3221225477 -and $null -eq $state.native_probe_exit) 'game fault remains distinct from bootstrap success and unknown probe result, including early-stop cleanup'
            if($scenario -eq 'game_process_fault'){Check ($state.native_exit_code -eq 0) 'process fault preserves the actual successful bootstrap exit code'}
            Check ($state.error.Contains('BC2 exited with process code 3221225477') -and -not $state.error.Contains('adapter') -and $state.resolution_cleanup -eq 'game_exited') 'game failure is not relabeled as adapter startup failure'
        }elseif($scenario -eq 'native_game_exit_live'){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.restart_required -eq $true -and $state.resolution_cleanup_pending -eq $true) 'contradictory native game-exit outcome cannot authorize release in a live game'
            Check ($state.resolution_cleanup -eq 'unconfirmed' -and -not(Test-Path -LiteralPath (Join-Path $run 'release-arguments.json'))) 'native outcome mismatch never invokes resolution release'
        }elseif($scenario -eq 'never_installed'){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.resolution_cleanup -eq 'never_installed' -and -not $state.resolution_guard_released) 'partial setup with no armed guard has a truthful never-installed outcome'
        }elseif($scenario -eq 'game_exits_during_release'){
            Check ($code -eq 0 -and $state.state -eq 'ended' -and -not $state.resolution_guard_released) ($scenario+' has a distinct truthful cleanup outcome')
        }elseif($scenario -in @('wrong_release_token','release_not_drained','release_timeout','missing_release_report','false_game_exited','native_cleanup_timeout')){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.resolution_cleanup_pending -eq $true -and $state.restart_required -eq $true) ($scenario+' cannot falsely report guard release')
            if($scenario -eq 'native_cleanup_timeout'){Check (-not(Test-Path -LiteralPath (Join-Path $run 'release-arguments.json'))) 'pending native adapter prevents a competing guard release'}
        }elseif($scenario -like 'native_exception_*'){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.native_exit_code -eq 2 -and 'running' -in $history) ($scenario+' reports failure after readiness')
            Check ($state.error.Contains('3221225477') -and $state.error.Contains('Report feedback')) ($scenario+' retains the actual probe exception and feedback guidance')
            if($scenario -eq 'native_exception_game_live'){
                Check ($state.restart_required -eq $true -and $state.error.Contains('Restart BC2 before launch') -and $state.error.Contains('reinstalling BC2VR is unnecessary',[StringComparison]::OrdinalIgnoreCase)) 'failed session with retained live game gives precise retry guidance'
            }else{
                Check (-not $state.ContainsKey('restart_required') -and -not $state.error.Contains('BC2 remains open')) 'game already closed does not receive a false restart requirement'
            }
        }else{
            Check ($code -ne 0 -and $state.state -eq 'failed' -and 'running' -notin $history) ($scenario+' never reports running')
            if($scenario -eq 'native_failure'){Check ($state.error.Contains('Fixture native profile mismatch')) 'actual native attach error survives into launcher state'}
            if($scenario -eq 'wrong_renderer'){Check ($state.error.Contains('DxVersion=11')) 'renderer error has actionable detail in launcher state'}
            if($scenario -in @('invalid_plan','resolution_failure','resolution_hook_live','resolution_callback_live','resolution_timeout','wrong_native_size','wrong_native_pid','renderer_did_not_resize',
                'missing_setup_report','wrong_guard_token','wrong_guard_supervisor','stale_guard_game','stale_guard_supervisor','guard_not_armed','missing_guard_proof')){
                Check (-not(Test-Path -LiteralPath (Join-Path $run 'host-arguments.json')) -and -not(Test-Path -LiteralPath (Join-Path $run 'session-telemetry.jsonl'))) ($scenario+' never starts headset or native adapter')
            }
            if($scenario -eq 'invalid_plan'){Check (-not(Test-Path -LiteralPath (Join-Path $run 'game-created.txt'))) 'invalid resolution plan fails before game startup'}
            if($scenario -eq 'resolution_timeout'){Check ($state.resolution_cleanup_pending -eq $true -and $state.restart_required -eq $true) 'pending native helper is never killed and requires a fresh game'}
            if($scenario -eq 'changed_xr_recommendation'){Check (-not(Test-Path -LiteralPath (Join-Path $run 'session-telemetry.jsonl'))) 'headset size changing after preflight cannot attach mismatched native source'}
            if($scenario -in @('missing_setup_report','wrong_guard_token','wrong_guard_supervisor','stale_guard_game','stale_guard_supervisor','guard_not_armed','missing_guard_proof')){
                Check ($state.resolution_cleanup -eq 'released' -and (Test-Path -LiteralPath (Join-Path $run 'release-arguments.json'))) ($scenario+' releases using the original persisted lease, independent of setup proof')
            }
        }
    }
    $passed=$true
    Write-Host "Portable session checks passed: $script:checks"
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if($passed){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}
}

#requires -Version 7.5
# Run the real session coordinator in child PowerShell processes. The fixture
# runtime replaces all game/XR/native process operations; no game is accessed.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$root=Join-Path $repo ('test-temp/session-start-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
try {
    $null=Set-FvrLauncherStorage $root
    $app=Join-Path $root 'package/app';$runtime=Join-Path $root 'package/runtime';$bin=Join-Path $runtime 'bin'
    foreach($p in @($app,$runtime,$bin)){[IO.Directory]::CreateDirectory($p)|Out-Null}
    foreach($name in @('Launcher.Core.ps1','Launcher.Storage.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination $app}
    foreach($name in @('Session.ps1','Startup.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/runtime/'+$name)) -Destination $runtime}
    [IO.File]::WriteAllText((Join-Path $bin 'BC2NativeProbe.dll'),'fixture data; never loaded')
    $fake=@'
function Assert-Bc2RuntimeSpace($Root){}
function Get-Bc2RuntimeConfig {return Read-FvrJson (Join-Path $PSScriptRoot 'fixture.json')}
function Test-Bc2AssetCache($Cache,$Recipe,$Hash){return $true}
function Get-Bc2AudioEnvironment($Cache){return @{fixture='true'}}
function Get-Bc2ProcessIdentity($Process){return @{pid=$Process.Id;start_utc='2026-10-18T00:00:00Z';path=$Process.Path}}
function Find-Bc2OwnedProcess($Identity){return $script:fakeGame}
function Get-Process {param($Name,$Id,$ErrorAction) if($Id){return [pscustomobject]@{Id=$Id;Path='fixture-supervisor'}}}
function New-FakeProcess($Id,$Name,$Exited=$false){
    $p=[pscustomobject]@{Id=$Id;Path=$Name;HasExited=$Exited;ExitCode=0;StartTime=[DateTime]'2026-10-18T00:00:00Z'}
    $p|Add-Member ScriptMethod WaitForExit {param($Timeout)return $true}
    return $p
}
function Start-Process {param($FilePath,$WorkingDirectory,$Environment,[switch]$PassThru)
    [IO.File]::WriteAllText((Join-Path $folder 'game-created.txt'),'fixture only')
    $script:fakeGame=New-FakeProcess 100 'BFBC2Game.exe';return $script:fakeGame
}
function Invoke-FixturePython {
    $out=$args[[Array]::IndexOf($args,'--output')+1]
    $cfg=Get-Bc2RuntimeConfig
    Write-FvrJson $out @{passed=($cfg.scenario -ne 'wrong_renderer');width=2560;height=1440;error='Direct3D 11 required; set DxVersion=11.'}
    $global:LASTEXITCODE=if($cfg.scenario -eq 'wrong_renderer'){1}else{0}
}
function Start-Bc2Child($File,$Arguments,$Folder,$LogName,$Environment=@{}){
    $cfg=Get-Bc2RuntimeConfig
    if($LogName -eq 'host'){
        Write-FvrJson (Join-Path $Folder 'host-arguments.json') @{arguments=$Arguments;environment=$Environment}
        $w=[int]$Arguments[[Array]::IndexOf($Arguments,'--eye-width')+1];$h=[int]$Arguments[[Array]::IndexOf($Arguments,'--eye-height')+1]
        Write-FvrJson (Join-Path $Folder 'ready.json') @{session_created=$true;width=$(if($cfg.scenario -eq 'bad_host_size'){1920}else{$w});height=$h}
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
    $native|Add-Member ScriptMethod WaitForExit {
        param($Timeout)
        $this.HasExited=$true
        if($this.FixtureScenario -in @('normal_game_close','native_exception_game_gone')){$script:fakeGame.HasExited=$true}
        if($this.FixtureScenario -in @('native_exception_game_gone','native_exception_game_live')){
            $this.ExitCode=2
            [IO.File]::WriteAllText((Join-Path $folder 'native.stdout.log'),'{"pid":100,"probe_exit":3221225477,"module_retained_until_game_exit":true,"game_files_modified":false}')
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
    foreach($scenario in @('success','native_failure','bad_host_size','wrong_renderer','normal_game_close','native_exception_game_gone','native_exception_game_live')){
        $install=Join-Path $root $scenario;$stateRoot=Join-Path $install 'data/runtime';$run=Join-Path $stateRoot 'sessions/current'
        $game=Join-Path $install 'game';$cache=Join-Path $stateRoot 'cache'
        foreach($p in @($run,$game,$cache)){[IO.Directory]::CreateDirectory($p)|Out-Null}
        $exe=Join-Path $game 'BFBC2Game.exe';[IO.File]::WriteAllText($exe,'fixture data; never executed')
        [IO.File]::WriteAllText((Join-Path $cache 'body-ammo-assets.fvrprop'),'fixture data')
        $hash=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
        Write-FvrJson (Join-Path $runtime 'fixture.json') @{scenario=$scenario;version='0.1.6';source_sha256=('a'*64);game_sha256=@($hash);asset_recipe_sha256=('b'*64)}
        $options=Join-Path $run 'options.json'
        Write-FvrJson $options @{state_root=$stateRoot;relative_folder='sessions/current';game_path=$game;game_sha256=$hash;cache=$cache;python='Invoke-FixturePython';activate_window=$false;version='0.1.6';source_sha256=('a'*64);xr_manifest='fixture-openxr.json';xr_label='Virtual Desktop fixture'}
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File (Join-Path $runtime 'Session.ps1') -OptionsPath $options *> (Join-Path $run 'coordinator.log')
        $code=$LASTEXITCODE
        $state=Read-FvrJson (Join-Path $run 'session.json');$history=Get-Content -LiteralPath (Join-Path $run 'state-history.txt')
        if($scenario -eq 'success'){
            Check ($code -eq 0 -and $state.state -eq 'ended' -and 'running' -in $history) 'actual session coordinator requires native input then completes cleanly'
            $hostCall=Read-FvrJson (Join-Path $run 'host-arguments.json')
            $w=[Array]::IndexOf($hostCall.arguments,'--eye-width');$h=[Array]::IndexOf($hostCall.arguments,'--eye-height')
            Check ($hostCall.arguments[$w+1] -eq '2560' -and $hostCall.arguments[$h+1] -eq '1440') 'non-1080p live dimensions reach actual host invocation'
            Check ($hostCall.environment.XR_RUNTIME_JSON -eq 'fixture-openxr.json') 'session uses same selected OpenXR runtime as preflight'
        }elseif($scenario -eq 'normal_game_close'){
            Check ($code -eq 0 -and $state.state -eq 'ended' -and $state.native_exit_code -eq 0 -and -not $state.error -and 'running' -in $history) 'normal game close after readiness remains a clean session end'
        }elseif($scenario -like 'native_exception_*'){
            Check ($code -ne 0 -and $state.state -eq 'failed' -and $state.native_exit_code -eq 2 -and 'running' -in $history) ($scenario+' reports failure after readiness')
            Check ($state.error.Contains('3221225477') -and $state.error.Contains('Report feedback')) ($scenario+' retains the actual probe exception and feedback guidance')
        }else{
            Check ($code -ne 0 -and $state.state -eq 'failed' -and 'running' -notin $history) ($scenario+' never reports running')
            if($scenario -eq 'native_failure'){Check ($state.error.Contains('Fixture native profile mismatch')) 'actual native attach error survives into launcher state'}
            if($scenario -eq 'wrong_renderer'){Check ($state.error.Contains('DxVersion=11')) 'renderer error has actionable detail in launcher state'}
        }
    }
    Write-Host "Portable session checks passed: $script:checks"
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}

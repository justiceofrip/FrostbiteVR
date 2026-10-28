#requires -Version 7.5
# Real Entry and restart helpers with process/XR/preparation boundaries replaced.
# No game, native executable, headset, installation or network operation occurs.
param([switch]$KeepArtifacts)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$root=Join-Path $repo ('test-temp/portable-restart-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$checks=[Collections.Generic.List[string]]::new();$passed=$false
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$checks.Add($Name);Write-Host "PASS $Name"}
$boundary=@'
$script:scenario=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'scenario.txt') -Raw
$script:events=[Collections.Generic.List[string]]::new()
function Event($Name){$script:events.Add($Name);[IO.File]::WriteAllLines((Join-Path $PSScriptRoot 'events.txt'),$script:events)}
function Get-Bc2RuntimeConfig {return @{version='0.1.9';source_sha256=('a'*64);asset_recipe_sha256=('b'*64)}}
function Resolve-Bc2Tool {return 'Invoke-FixturePython'}
function Invoke-FixturePython {$global:LASTEXITCODE=0}
function Test-Bc2AssetCache {return $true}
function Assert-Bc2RuntimeSpace {}
function New-ProcessFixture($Id,$Path) {
    $p=[pscustomobject]@{Id=$Id;Path=$Path;StartTime=[DateTimeOffset]::Parse('2026-10-20T10:14:31Z').UtcDateTime;HasExited=$false;RestartFixture=$true}
    $p|Add-Member ScriptMethod CloseMainWindow {Event 'normal_close';return ($script:scenario -ne 'close_refused')}
    $p|Add-Member ScriptMethod WaitForExit {param($Milliseconds) Event 'wait_game_exit';$this.HasExited=$true;return $true}
    $p|Add-Member ScriptMethod Kill {throw 'Fixture forbids forced termination'}
    return $p
}
# Replace the OS observation boundary only. The real ownership, path, birth
# and session-record comparisons below still consume these observed values.
# Real retained-handle behavior is covered by Test-Bc2ProcessIdentity.ps1.
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive=$false) {
    if($Process -isnot [pscustomobject] -or -not $Process.RestartFixture){throw 'Only restart fixture processes may reach this boundary'}
    if($RequireLive -and $Process.HasExited){throw 'Fixture process is no longer live'}
    if(-not [IO.Path]::IsPathFullyQualified($Process.Path)){throw 'Fixture process requires an absolute observed image path'}
    return @{pid=$Process.Id;start_utc=$Process.StartTime.ToUniversalTime().ToString('o');path=$Process.Path}
}
$script:game=New-ProcessFixture 39252 (Join-Path $PSScriptRoot 'game/BFBC2Game.exe')
$script:supervisor=New-ProcessFixture 39612 (Join-Path $PSScriptRoot 'fixture-pwsh.exe')
if($script:scenario -eq 'wrong_game'){$script:game.Path='X:\Other\BFBC2Game.exe'}
if($script:scenario -eq 'active_reused_pid'){$script:supervisor.StartTime=$script:supervisor.StartTime.AddSeconds(1)}
if($script:scenario -eq 'active_wrong_path'){$script:supervisor.Path=Join-Path $PSScriptRoot 'other-pwsh.exe'}
if($script:scenario -eq 'active_exited'){$script:supervisor.HasExited=$true}
function Get-Process {param([string[]]$Name,[int]$Id,$ErrorAction)
    if($Id){
        if($Id -eq $script:supervisor.Id -and $script:scenario -like 'active_*'){return $script:supervisor}
        return $null
    }
    if('BFBC2Game' -in $Name){
        if($script:scenario -eq 'multiple_games'){return @($script:game,$script:game)}
        if($script:scenario -in @('retained','restart','wrong_game','close_refused')){return $script:game}
        if($script:scenario -eq 'game_appears' -and 'preflight' -in $script:events){return $script:game}
    }
    return
}
function Start-Process {throw 'Fixture forbids real process launch'}
function Start-Bc2Child($File,$Arguments,$Folder,$LogName,$Environment=@{}) {
    if($LogName -ne 'supervisor'){throw 'Unexpected external helper'}
    Event 'new_supervisor'
    Write-FvrJson (Join-Path $Folder 'session.json') @{state='running';phase='fixture connected'}
    return (New-ProcessFixture 50000 (Join-Path $PSScriptRoot 'fixture-new-supervisor.exe'))
}
'@
$startup=@'
function Test-Bc2GameInstallation {return @{exe=(Join-Path $PSScriptRoot 'game/BFBC2Game.exe');sha256=('c'*64)}}
function Get-Bc2OpenXrSelection {return @{manifest='fixture-runtime.json';label='fixture only'}}
function Invoke-Bc2OpenXrPreflight {Event 'preflight';return @{instance_created=$true;system_available=$true;eye_width=2784;eye_height=3024;recommended_eye_width=2784;recommended_eye_height=3024;max_eye_width=8192;max_eye_height=8192}}
'@
$dependencies=@'
function Get-Bc2PreparationFfmpeg {Event 'dependency';return 'fixture-decoder-never-executed'}
'@
try {
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $root -Operation Session -PlannedBytes 2MB -RawCaptureBytes 0
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage admission required'}
    $null=Set-FvrLauncherStorage $root
    $app=Join-Path $root 'package/app';$runtime=Join-Path $root 'package/runtime'
    foreach($p in @($app,$runtime)){[IO.Directory]::CreateDirectory($p)|Out-Null}
    foreach($name in @('Launcher.Core.ps1','Launcher.Storage.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination $app}
    foreach($name in @('Entry.ps1','Runtime.Core.ps1','Resolution.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/runtime/'+$name)) -Destination $runtime}
    [IO.File]::AppendAllText((Join-Path $runtime 'Runtime.Core.ps1'),"`n"+$boundary)
    [IO.File]::WriteAllText((Join-Path $runtime 'Startup.ps1'),$startup)
    [IO.File]::WriteAllText((Join-Path $runtime 'Dependencies.ps1'),$dependencies)
    foreach($scenario in @('retained','restart','wrong_game','multiple_games','close_refused','game_appears','stale','active_running','active_failed','active_ended','active_mismatch','active_reused_pid','active_wrong_path','active_exited')){
        [IO.File]::WriteAllText((Join-Path $runtime 'scenario.txt'),$scenario)
        [IO.File]::WriteAllText((Join-Path $runtime 'events.txt'),'')
        $install=Join-Path $root $scenario;$state=Join-Path $install 'data/runtime'
        $old=Join-Path $state 'sessions/prior';[IO.Directory]::CreateDirectory($old)|Out-Null
        $identity=@{pid=39612;path=(Join-Path $runtime 'fixture-pwsh.exe');start_utc='2026-10-20T10:14:31.0000000Z'}
        Write-FvrJson (Join-Path $state 'active-session.json') @{schema='bc2vr.active-session.v1';version='0.1.9';relative_folder='sessions/prior';supervisor=$identity}
        $previous=@{schema='bc2vr.session.v1';version='0.1.9';state='failed';supervisor=$identity.Clone()}
        if($scenario -eq 'active_running'){$previous.state='running'}
        if($scenario -eq 'active_ended'){$previous.state='ended'}
        if($scenario -eq 'active_mismatch'){$previous.supervisor.start_utc='2026-10-19T10:14:31.0000000Z'}
        Write-FvrJson (Join-Path $old 'session.json') $previous
        $settings=Join-Path $install 'data/settings.json';Write-FvrJson $settings @{game_path=(Join-Path $runtime 'game');sentinel='preserved'}
        $settingsHash=(Get-FileHash -LiteralPath $settings).Hash
        $args=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $runtime 'Entry.ps1'),'-Action','Launch','-SettingsPath',$settings,'-StateRoot',$state)
        if($scenario -in @('restart','wrong_game','close_refused')){$args+='-Fresh'}
        & (Join-Path $PSHOME 'pwsh.exe') @args *> (Join-Path $install 'result.log')
        $code=$LASTEXITCODE;$text=Get-Content -Raw -LiteralPath (Join-Path $install 'result.log')
        $events=@(Get-Content -LiteralPath (Join-Path $runtime 'events.txt'))
        Write-FvrJson (Join-Path $install 'events.json') $events
        if($scenario -in @('retained','wrong_game','multiple_games')){
            Check ($code -ne 0 -and 'preflight' -notin $events -and 'new_supervisor' -notin $events -and 'normal_close' -notin $events) ($scenario+' rejected before XR/preparation without closing a game')
            if($scenario -eq 'retained'){Check ($text.Contains('Restart BC2 before launch') -and $text.Contains('reinstalling BC2VR is unnecessary')) 'retained-game retry gives the recovery action, not a reinstall'}
        }elseif($scenario -eq 'restart'){
            Check ($code -eq 0 -and ($events -join ',') -eq 'preflight,dependency,normal_close,wait_game_exit,new_supervisor') 'explicit Fresh performs one normal close and starts a distinct session'
            Check ((Read-FvrJson (Join-Path $state 'active-session.json')).relative_folder -ne 'sessions/prior') 'retry uses a fresh channel/session directory'
        }elseif($scenario -eq 'close_refused'){
            Check ($code -ne 0 -and 'normal_close' -in $events -and 'new_supervisor' -notin $events -and $text.Contains('Close it yourself')) 'normal-close refusal never forces termination or starts another session'
        }elseif($scenario -eq 'game_appears'){
            Check ($code -ne 0 -and 'preflight' -in $events -and 'normal_close' -notin $events -and 'new_supervisor' -notin $events) 'game appearing during preparation is rechecked with unchanged Fresh consent'
        }elseif($scenario -eq 'stale'){
            Check ($code -eq 0 -and 'new_supervisor' -in $events) 'dead prior supervisor and retained record do not require deletion or reinstall'
        }elseif($scenario -in @('active_reused_pid','active_wrong_path','active_exited')){
            Check ($code -eq 0 -and 'new_supervisor' -in $events -and 'normal_close' -notin $events -and -not $text.Contains('already running')) ($scenario+' does not grant stale supervisor ownership')
        }elseif($scenario -in @('active_failed','active_ended')){
            Check ($code -ne 0 -and $text.Contains('finishing cleanup') -and 'preflight' -notin $events) ($scenario+' cannot falsely report running while cleanup is finishing')
        }else{
            Check ($code -eq 0 -and $text.Contains('already running') -and 'preflight' -notin $events) ($scenario+' preserves live supervisor and rejects unrelated terminal-state evidence')
        }
        Check ((Get-FileHash -LiteralPath $settings).Hash -ceq $settingsHash) ($scenario+' preserves settings')
    }
    $passed=$true
    Write-Host "Portable restart checks passed: $($checks.Count)"
}finally{
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if(Test-Path -LiteralPath $root){
        Write-FvrJson (Join-Path $root 'validation.json') @{passed=$passed;checks=@($checks);game_started=$false;headset_accessed=$false;user_install_changed=$false}
        if($passed -and -not $KeepArtifacts){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}
    }
}

#requires -Version 7.5
param([string]$RepositoryRoot=(Split-Path $PSScriptRoot -Parent))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot 'tools/Invoke-Bc2XrGameSimulation.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Runner does not parse'}
# Execute the real cleanup branch against inert retained objects, never the runner.
$blocks=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($owned.ContainsKey(''game'')){') -and $n.Extent.Text.Contains('$game=$owned.game.process')},$true))
if($blocks.Count -ne 1){throw 'Expected one production game-cleanup branch'}
$cleanup=[scriptblock]::Create($blocks[0].Extent.Text)
$status=$ast.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$receipt.status'},$true)
if(-not $status){throw 'Missing final status assignment'}
$finalStatus=[scriptblock]::Create($status.Extent.Text)
foreach($name in 'Require','SameIdentity'){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing production function $name"}
    . ([scriptblock]::Create($fn.Extent.Text))
}
function NoOtherProcesses {Require (-not $script:f.unowned) 'Unowned process fixture'}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive){return $script:f.identity}
function Pin([string]$Path,[long]$Maximum){return @{sha256=$script:f.hash}}
function Json([string]$Path,[long]$Maximum){
    if([IO.Path]::GetFileName($Path) -ceq 'native-trace.json'){return $script:f.native}
    $script:f.releaseReads++
    Require $script:f.releasePresent 'Missing resolution release fixture'
    return @{fixture=$true}
}
function Save([string]$Name,$Value){$script:f.saved[$Name]=$Value}
function Read-Bc2NativeRunOutcome([string]$Path,[uint32]$ExpectedPid){
    # Real reader schema/type checks are covered by Test-NativeFailureDiagnostics.
    if($script:f.bootstrap -and $script:f.bootstrap.pid -eq $ExpectedPid){return $script:f.bootstrap}
    return $null
}
function Assert-Bc2ResolutionIdentity($Observed,$Lease,[string]$Operation){Require $script:f.releaseValid 'Invalid release identity fixture'}
function Assert-Bc2ResolutionReleased($Observed,$Lease){$script:f.releaseChecks++;Require $script:f.releaseValid 'Invalid release fixture'}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++}
function Run([string]$Name,[scriptblock]$Change,[bool]$Allowed,[bool]$PreActivation=$true){
    $identity=@{pid=17088;start_utc='2026-11-04T22:24:41.3191870Z';path='G:\fixture\BFBC2Game.exe'}
    $gameObject=[pscustomobject]@{Id=17088;HasExited=$false;ExitCode=0;closeCalls=0}
    $gameObject|Add-Member ScriptMethod CloseMainWindow {$this.closeCalls++;return $true}
    $gameObject|Add-Member ScriptMethod WaitForExit {param($Milliseconds) $this.HasExited=$true;return $true}
    $script:f=@{
        native=@{state='failed';error='Native Present graphics bootstrap unavailable';hooks_disabled=$true}
        bootstrap=@{pid=17088;outcome='remote_thread_exited';remote_thread_exit_observed=$true;probe_exit=20;report_nonempty=$true}
        trace=[pscustomobject]@{HasExited=$true;ExitCode=2};identity=$identity.Clone();hash='original';unowned=$false
        releasePresent=$false;releaseValid=$true;releaseReads=0;releaseChecks=0;saved=@{}
    }
    & $Change $script:f
    $owned=@{game=@{process=$gameObject;identity=$identity};trace=@{process=$script:f.trace}}
    $session=@{trace=@{pid=51320};resolution_lease=@{pid=17088}}
    $folder='G:\fixture\session';$gameExe=$identity.path;$gamePin=@{sha256='original'}
    $clock=[Diagnostics.Stopwatch]::StartNew();$TimeoutSeconds=420
    $failures=[Collections.Generic.List[string]]::new();$nativeRetired=$false;$gameClosed=$false;$caught=$null
    try{. $cleanup}catch{$caught=$_.Exception.Message}
    if($Allowed){
        Check ($null -eq $caught -and $gameObject.closeCalls -eq 1 -and $gameObject.HasExited -and $gameClosed) "$Name normal close: $caught"
        $row=$script:f.saved['native-retirement.json']
        Check ($row.pre_activation_refusal -eq $PreActivation -and $row.reload_flow_drained -eq (-not $PreActivation) -and $nativeRetired -eq (-not $PreActivation) -and $row.native_retirement_proven -eq $nativeRetired) "$Name truthful retirement"
        Check ($script:f.releaseReads -eq [int](-not $PreActivation) -and $script:f.releaseChecks -eq [int](-not $PreActivation)) "$Name resolution requirement"
        $receipt=@{};$runningObserved=$true;$simObserved=$true;$restored=$true;$launcherPreserved=$true
        . $finalStatus
        Check ($receipt.status -ceq $(if($PreActivation){'failed'}else{'observed_and_closed'})) "$Name overall status"
        Check (($failures.Count -gt 0) -eq $PreActivation) "$Name failure retained"
    }else{
        Check ($null -ne $caught -and $gameObject.closeCalls -eq 0 -and -not $gameClosed -and -not $gameObject.HasExited) "$Name refuses close"
    }
}
# Saved actual408: no gameplay section or resolution-release receipt.
Run 'actual408' {} $true
Run 'existing menu refusal' {param($f) $f.native.error='Main-menu startup requires native menu capture and input'} $true
foreach($suffix in @('swap-chain device query or identity failed','no immediate context','missing context method','renderer changed during capture','no Present callback before deadline','only foreign swap chains presented','only test presents on the expected chain','expected chain no longer matches the renderer','capture did not publish before deadline')){
    Run $suffix ({param($f) $f.native.error='Native Present graphics bootstrap unavailable: '+$suffix}.GetNewClosure()) $true
}
$refusals=[ordered]@{
    unknown={param($f) $f.native.error='Native gameplay binding unverified'}
    suffix_spoof={param($f) $f.native.error+=': no Present callback before deadline EXTRA'}
    generic_suffix={param($f) $f.native.error+=': unknown'}
    case_spoof={param($f) $f.native.error=$f.native.error.ToUpperInvariant()}
    error_array={param($f) $f.native.error=@($f.native.error)}
    missing_error={param($f) $f.native.Remove('error')}
    wrong_state={param($f) $f.native.state='observed'}
    state_array={param($f) $f.native.state=@('failed')}
    active_hooks={param($f) $f.native.hooks_disabled=$false}
    missing_hooks={param($f) $f.native.Remove('hooks_disabled')}
    string_hooks={param($f) $f.native.hooks_disabled='true'}
    wrong_probe={param($f) $f.bootstrap.probe_exit=12}
    success_probe={param($f) $f.bootstrap.probe_exit=0}
    string_probe={param($f) $f.bootstrap.probe_exit='20'}
    success_trace={param($f) $f.trace.ExitCode=0}
    wrong_trace={param($f) $f.trace.ExitCode=1}
    live_trace={param($f) $f.trace.HasExited=$false}
    missing_outcome={param($f) $f.bootstrap=$null}
    game_exit={param($f) $f.bootstrap.outcome='game_exited'}
    remote_pending={param($f) $f.bootstrap.remote_thread_exit_observed=$false}
    string_remote={param($f) $f.bootstrap.remote_thread_exit_observed='true'}
    empty_report={param($f) $f.bootstrap.report_nonempty=$false}
    string_report={param($f) $f.bootstrap.report_nonempty='true'}
    wrong_target={param($f) $f.bootstrap.pid=17089}
    reused_pid={param($f) $f.identity.start_utc='2026-11-04T23:00:00.0000000Z'}
    wrong_path={param($f) $f.identity.path='G:\other\BFBC2Game.exe'}
    changed_image={param($f) $f.hash='changed'}
    unowned_process={param($f) $f.unowned=$true}
}
foreach($name in $refusals.Keys){Run $name $refusals[$name] $false}
$observed={param($f)
    $f.native=@{state='observed';pid=17088;hooks_disabled=$true;gameplay=@{reload_flow=@{drained=$true;in_flight=0}}}
    $f.bootstrap.probe_exit=0;$f.trace.ExitCode=0;$f.releasePresent=$true
}
Run 'observed gameplay' $observed $true $false
foreach($fault in @('undrained','in_flight','wrong_pid','missing_gameplay','missing_release','bad_release')){
    Run ('gameplay '+$fault) ({param($f)
        & $observed $f
        switch($fault){
            undrained {$f.native.gameplay.reload_flow.drained=$false}
            in_flight {$f.native.gameplay.reload_flow.in_flight=1}
            wrong_pid {$f.native.pid=17089}
            missing_gameplay {$f.native.Remove('gameplay')}
            missing_release {$f.releasePresent=$false}
            bad_release {$f.releaseValid=$false}
        }
    }.GetNewClosure()) $false
}
Write-Host "XR game cleanup checks passed: $script:checks; no processes or files changed."

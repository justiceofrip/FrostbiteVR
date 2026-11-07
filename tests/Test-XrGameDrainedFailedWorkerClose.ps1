#requires -Version 7.5
param(
    [Parameter(Mandatory)][string]$RunnerPath,
    [Parameter(Mandatory)][string]$StartupPath,
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][string]$NativeDirectory,
    [Parameter(Mandatory)][string]$ScratchDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$utf8=[Text.UTF8Encoding]::new($false)
$checks=0;$cases=[Collections.Generic.List[object]]::new()
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++}
function Parse([string]$Path){
    $tokens=$null;$errors=$null
    $parsed=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) "Parse $Path";return $parsed
}
$ast=Parse $RunnerPath;$startupAst=Parse $StartupPath
$entryBranch=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($entry){')},$true)
$cleanup=[scriptblock]::Create($entryBranch.Clauses[0].Item2.Statements[0].Body.Extent.Text.Trim().Substring(1).TrimEnd('}'))
$outputBranch=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($outputReady){')},$true)
$restore=[scriptblock]::Create($outputBranch.Clauses[0].Item2.Statements[0].Body.Extent.Text.Trim().Substring(1).TrimEnd('}'))
$launcherCheck=[scriptblock]::Create($outputBranch.Clauses[0].Item2.Statements[1].Body.Extent.Text.Trim().Substring(1).TrimEnd('}'))
$status=$ast.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$receipt.status'},$true)
$finalStatus=[scriptblock]::Create($status.Extent.Text)
foreach($name in 'Require','SameIdentity','Backup'){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    Check ($null -ne $fn) "Production function $name";. ([scriptblock]::Create($fn.Extent.Text))
}
$outcomeFn=$startupAst.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Read-Bc2NativeRunOutcome'},$true)
. ([scriptblock]::Create($outcomeFn.Extent.Text.Replace('function Read-Bc2NativeRunOutcome(', 'function Read-ActualNativeOutcome(')))
function Assert-FvrNoLinks([string]$Path){}
function RequestStop([string]$Reason){}
function ObserveSession {}
function NoOtherProcesses([switch]$BeforeLaunch,[switch]$RetainedOnly){
    Require (-not $script:f.unowned) 'Unowned process fixture'
    if($RetainedOnly){$script:f.strictScans++;Require (-not $script:f.unretainedChild) 'Unretained child fixture'}
}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive){
    $script:f.identityReads++
    if($script:f.lateIdentity -and $script:f.identityReads -gt 1){$script:f.identity.start_utc='2026-11-08T00:00:00.0000000Z'}
    return $script:f.identity
}
function Full([string]$Path){
    $full=[IO.Path]::GetFullPath($Path)
    Require ($full.StartsWith([IO.Path]::GetFullPath($ScratchDirectory)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Fixture write outside scratch'
    return $full
}
function Pin([string]$Path,[long]$Maximum=4MB){
    if($Path -ieq $gameExe){return @{sha256=$script:f.imageHash}}
    if([IO.Path]::GetFileName($Path) -ceq 'native-trace.json'){return @{path=$Path;bytes=$script:f.nativeBytes;sha256=$nativeHash}}
    $Path=Full $Path
    return @{path=$Path;bytes=(Get-Item -LiteralPath $Path).Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Json([string]$Path,[long]$Maximum){
    Require ([IO.Path]::GetFileName($Path) -ceq 'native-trace.json') 'Failed worker must not release resolution guard'
    return $script:f.native
}
function Save([string]$Name,$Value){$script:f.saved[$Name]=$Value}
function Read-Bc2NativeRunOutcome([string]$Path,[uint32]$ExpectedPid){return Read-ActualNativeOutcome $script:f.bootstrapPath $ExpectedPid}
$resultPath=Join-Path $RunDirectory 'result.json';$result=[IO.File]::ReadAllText($resultPath)|ConvertFrom-Json -AsHashtable -Depth 100
$sessionText=[IO.File]::ReadAllText((Join-Path $RunDirectory 'session-final.json'))
$recovery=[IO.File]::ReadAllText((Join-Path $RunDirectory 'root-recovery/result.json'))|ConvertFrom-Json -AsHashtable
$nativePath=Join-Path $NativeDirectory 'native-trace.json';$nativeText=[IO.File]::ReadAllText($nativePath)
$nativeHash=(Get-FileHash -LiteralPath $nativePath -Algorithm SHA256).Hash.ToLowerInvariant()
$bootstrapPath=Join-Path $NativeDirectory 'native.stdout.log'
$originalHash=(Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash.ToLowerInvariant()
$originalNative=$nativeText|ConvertFrom-Json -AsHashtable -Depth 100
Check ($result.status -ceq 'failed' -and -not $result.native_retirement_proven -and -not $result.reload_success_claimed) 'Actual644 remains failed'
Check ($originalNative.hooks_disabled -is [bool] -and $originalNative.hooks_disabled -and $originalNative.gameplay.reload_flow.drained -is [bool] -and $originalNative.gameplay.reload_flow.drained -and $originalNative.gameplay.reload_flow.in_flight -eq 0) 'Actual644 disabled and drained evidence'
Check ($recovery.original_failure_sha256 -ceq $originalHash -and $recovery.original_failure_unchanged -and $recovery.game_absent_after_normal_close_request -and $recovery.exact_three_profiles_restored -and -not $recovery.native_retirement_proven -and -not $recovery.forced_termination) 'Actual644 ROOT recovery preserves failure'
$null=[IO.Directory]::CreateDirectory($ScratchDirectory)
function RunCase([string]$Name,[scriptblock]$Change,[bool]$AllowClose,[bool]$ExpectRestored=$AllowClose){
    $OutputDir=Join-Path $ScratchDirectory $Name;$null=[IO.Directory]::CreateDirectory($OutputDir)
    $session=$sessionText|ConvertFrom-Json -AsHashtable -Depth 100
    $script:f=@{identity=$session.game.Clone();native=($nativeText|ConvertFrom-Json -AsHashtable -Depth 100);nativeBytes=(Get-Item -LiteralPath $nativePath).Length;bootstrapPath=$bootstrapPath;bootstrap=$null;imageHash='original';unowned=$false;unretainedChild=$false;strictScans=0;saved=@{};elapsed=$result.elapsed_seconds;closeAccepted=$true;waitExit=$true;lateIdentity=$false;identityReads=0;changedSettings=$false}
    $owned=@{};foreach($row in $result.processes){$owned[$row.name]=@{identity=$row.identity;process=[pscustomobject]@{Id=$row.identity.pid;HasExited=$row.exited;ExitCode=$row.exit_code}}}
    $game=$owned.game.process;$game.ExitCode=0
    $game|Add-Member NoteProperty closeCalls 0;$game|Add-Member NoteProperty waited $null
    $game|Add-Member ScriptMethod CloseMainWindow {$this.closeCalls++;return $script:f.closeAccepted}
    $game|Add-Member ScriptMethod WaitForExit {param($Milliseconds) $this.waited=$Milliseconds;$this.HasExited=$script:f.waitExit;return $script:f.waitExit}
    $entry=$owned.entry.process;$folder='G:\inert-native-session';$gameExe=$owned.game.identity.path;$gamePin=@{sha256='original'}
    $preAttachSpec=$null;$stopReason='Original stop request';$TimeoutSeconds=[int]$result.limits.outer_seconds
    $failures=[Collections.Generic.List[string]]::new();foreach($failure in $result.failures){$failures.Add($failure)}
    $nativeRetired=$false;$gameClosed=$false;$restored=$false;$caught=$null;$restoreCaught=$null;$launcherCaught=$null;$launcherPreserved=$false
    $profiles=@();foreach($i in 0..2){
        $originalPath=Join-Path $OutputDir "profile$i.bin";$backupPath=Join-Path $OutputDir "backup$i.bin"
        [IO.File]::WriteAllBytes($originalPath,[byte[]]@(10,20,$i));[IO.File]::WriteAllBytes($backupPath,[byte[]]@(30,40,$i))
        $backupPin=Pin $backupPath;$profiles+=@{original=@{path=$originalPath;sha256=$backupPin.sha256};backup=$backupPin}
    }
    $launcherFiles=@();foreach($i in 0..1){$p=Join-Path $OutputDir "launcher$i.json";[IO.File]::WriteAllText($p,'original',$utf8);$launcherFiles+=@{original=(Pin $p)}}
    & $Change $script:f $owned $session
    if($script:f.changedSettings){[IO.File]::WriteAllText($launcherFiles[1].original.path,'another owner changed settings',$utf8)}
    $settingsBefore=(Pin $launcherFiles[1].original.path).sha256
    if($script:f.bootstrap){$script:f.bootstrapPath=Join-Path $OutputDir 'outcome.json';[IO.File]::WriteAllText($script:f.bootstrapPath,($script:f.bootstrap|ConvertTo-Json -Depth 10),$utf8)}
    $clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=$script:f.elapsed}}
    try{. $cleanup}catch{$caught=$_.Exception.Message}
    try{. $restore}catch{$restoreCaught=$_.Exception.Message}
    try{. $launcherCheck}catch{$launcherCaught=$_.Exception.Message}
    Check (($game.closeCalls -eq 1) -eq $AllowClose) "$Name normal close gate: $caught"
    Check ($restored -eq $ExpectRestored) "$Name restoration gate: $restoreCaught"
    foreach($row in $profiles){Check (((Pin $row.original.path).sha256 -ceq $row.original.sha256) -eq $ExpectRestored) "$Name exact profile bytes"}
    Check (-not $nativeRetired -and -not $script:f.saved.ContainsKey('native-retirement.json')) "$Name no clean retirement"
    Check (@($result.failures|Where-Object {-not $failures.Contains($_)}).Count -eq 0) "$Name original failure text retained"
    Check ((Pin $launcherFiles[1].original.path).sha256 -ceq $settingsBefore) "$Name never overwrites launcher settings"
    Check (($null -ne $launcherCaught) -eq $script:f.changedSettings -and $launcherPreserved -eq (-not $script:f.changedSettings)) "$Name launcher change remains refused"
    $receipt=@{reload_success_claimed=$false};$runningObserved=$true;$simObserved=$true;. $finalStatus
    Check ($receipt.status -ceq 'failed' -and -not $receipt.reload_success_claimed) "$Name remains failed"
    if($AllowClose){
        $saved=$script:f.saved['native-close-recovery.json']
        Check ($script:f.strictScans -eq 1 -and $saved.hooks_disabled -eq $script:f.native.hooks_disabled -and -not $saved.native_retirement_proven -and -not $saved.reload_flow_drain_claimed -and -not $saved.resolution_release_confirmed -and $saved.normal_close_only -and -not $saved.forced_termination) "$Name truthful failed-close receipt"
        if($script:f.closeAccepted){Check ($game.waited -ge 0 -and $game.waited -le 10000 -and $game.waited -le ($TimeoutSeconds-$script:f.elapsed)*1000) "$Name original deadline bound"}
    }else{Check ($null -ne $caught -and $null -ne $restoreCaught) "$Name refuses close and restoration"}
    $cases.Add(@{case=$Name;normal_close_called=($game.closeCalls -eq 1);restored=$restored;status=$receipt.status;native_retirement_proven=$nativeRetired;cleanup_error=$caught})
}
RunCase 'actual644-disabled-drained' {} $true
RunCase 'old-unresolved-hooks-control' {param($f) $f.native.hooks_disabled=$false;$f.native.gameplay.reload_flow.drained=$false} $true
$refusals=[ordered]@{
    native_pid={param($f) $f.native.pid++}
    native_string_pid={param($f) $f.native.pid=[string]$f.native.pid}
    reused_game={param($f) $f.identity.start_utc='2026-11-08T00:00:00.0000000Z'}
    late_reused_game={param($f) $f.lateIdentity=$true}
    changed_image={param($f) $f.imageHash='changed'}
    live_trace={param($f,$o) $o.trace.process.HasExited=$false}
    live_host={param($f,$o) $o.host.process.HasExited=$false}
    trace_exit_1={param($f,$o) $o.trace.process.ExitCode=1}
    trace_exit_0={param($f,$o) $o.trace.process.ExitCode=0}
    unowned={param($f) $f.unowned=$true}
    unretained_child={param($f) $f.unretainedChild=$true}
    deadline={param($f) $f.elapsed=540}
    report_length={param($f) $f.nativeBytes++}
    module_unretained={param($f) $f.native.module_retained_until_exit=$false}
    hooks_string={param($f) $f.native.hooks_disabled='true'}
    hooks_missing={param($f) $f.native.Remove('hooks_disabled')}
    undrained={param($f) $f.native.gameplay.reload_flow.drained=$false}
    drain_string={param($f) $f.native.gameplay.reload_flow.drained='true'}
    drain_missing={param($f) $f.native.gameplay.reload_flow.Remove('drained')}
    callbacks_active={param($f) $f.native.gameplay.reload_flow.in_flight=1}
    callbacks_string={param($f) $f.native.gameplay.reload_flow.in_flight='0'}
    callbacks_missing={param($f) $f.native.gameplay.reload_flow.Remove('in_flight')}
    session_running={param($f,$o,$s) $s.state='running'}
    resolution_released={param($f,$o,$s) $s.resolution_guard_released=$true}
    resolution_not_pending={param($f,$o,$s) $s.resolution_cleanup_pending=$false}
    resolution_not_unconfirmed={param($f,$o,$s) $s.resolution_cleanup='released'}
}
foreach($name in $refusals.Keys){RunCase $name $refusals[$name] $false}
foreach($field in @('pid','probe_exit','remote_thread_exit_observed','module_retained_until_game_exit','report_nonempty')){
    RunCase ('bootstrap-'+$field) ({param($f)
        $f.bootstrap=[IO.File]::ReadAllText($f.bootstrapPath)|ConvertFrom-Json -AsHashtable
        if($f.bootstrap[$field] -is [bool]){$f.bootstrap[$field]=$false}else{$f.bootstrap[$field]++}
    }.GetNewClosure()) $false
}
RunCase 'close_refused' {param($f) $f.closeAccepted=$false} $true $false
RunCase 'close_timeout' {param($f) $f.waitExit=$false;$f.elapsed=538} $true $false
RunCase 'changed_launcher_settings' {param($f) $f.changedSettings=$true} $true
Check ((Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $originalHash) 'Original failed result unchanged'
Check ((Get-FileHash -LiteralPath $nativePath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $nativeHash) 'Original native evidence unchanged'
$summary=@{evidence_level='deterministic_test';passed=$true;checks=$checks;cases=@($cases);actual_run=$RunDirectory;original_failure_sha256=$originalHash;native_sha256=$nativeHash;game_or_input_actions=$false;source_runner=(Get-FileHash -LiteralPath $RunnerPath -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText((Join-Path $ScratchDirectory 'result.json'),($summary|ConvertTo-Json -Depth 15),$utf8)
Write-Output "Drained failed-worker checks passed: $checks; $($cases.Count) actual-evidence replays; no real processes or profiles changed."

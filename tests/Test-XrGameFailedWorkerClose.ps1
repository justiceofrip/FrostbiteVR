#requires -Version 7.5
param(
    [Parameter(Mandatory)][string]$RunnerPath,
    [Parameter(Mandatory)][string]$StartupPath,
    [Parameter(Mandatory)][string[]]$RunDirectories,
    [Parameter(Mandatory)][string[]]$NativeDirectories,
    [Parameter(Mandatory)][string]$ScratchDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$utf8=[Text.UTF8Encoding]::new($false)
$checks=0;$cases=[Collections.Generic.List[object]]::new();$evidence=@()
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++}
function Parse([string]$Path){
    $tokens=$null;$errors=$null
    $parsed=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) "Parse $Path"
    return $parsed
}
$ast=Parse $RunnerPath
$startupAst=Parse $StartupPath
$entryBranch=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($entry){')},$true)
$cleanup=[scriptblock]::Create($entryBranch.Clauses[0].Item2.Statements[0].Body.Extent.Text.Trim().Substring(1).TrimEnd('}'))
$outputBranch=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($outputReady){')},$true)
$restore=[scriptblock]::Create($outputBranch.Clauses[0].Item2.Statements[0].Body.Extent.Text.Trim().Substring(1).TrimEnd('}'))
$status=$ast.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$receipt.status'},$true)
$finalStatus=[scriptblock]::Create($status.Extent.Text)
foreach($name in 'Require','SameIdentity','Backup','NoOtherProcesses'){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    Check ($null -ne $fn) "Production function $name"
    . ([scriptblock]::Create($fn.Extent.Text))
}
$productionScan=(Get-Item Function:NoOtherProcesses).ScriptBlock
$outcomeFn=$startupAst.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Read-Bc2NativeRunOutcome'},$true)
. ([scriptblock]::Create($outcomeFn.Extent.Text.Replace('function Read-Bc2NativeRunOutcome(', 'function Read-ActualNativeOutcome(')))
function Assert-FvrNoLinks([string]$Path){}
function RequestStop([string]$Reason){}
function ObserveSession {}
function NoOtherProcesses([switch]$BeforeLaunch,[switch]$RetainedOnly){
    Require (-not $script:f.unowned) 'Unowned process fixture'
    if($RetainedOnly){$script:f.strictScans++;Require (-not $script:f.unretainedChild) 'Unretained supervisor child fixture'}
}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive){return $script:f.identity}
function Full([string]$Path){
    $full=[IO.Path]::GetFullPath($Path)
    Require ($full.StartsWith([IO.Path]::GetFullPath($ScratchDirectory)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Fixture write outside scratch'
    return $full
}
function Pin([string]$Path,[long]$Maximum=4MB){
    if($Path -ieq $script:f.identity.path){return @{sha256=$script:f.imageHash}}
    if([IO.Path]::GetFileName($Path) -ceq 'native-trace.json'){return @{path=$Path;bytes=$script:f.nativeBytes;sha256=$script:f.nativeHash}}
    $Path=Full $Path
    return @{path=$Path;bytes=(Get-Item -LiteralPath $Path).Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Json([string]$Path,[long]$Maximum){
    Require ([IO.Path]::GetFileName($Path) -ceq 'native-trace.json') 'Failed worker must not release resolution guard'
    return $script:f.native
}
function Save([string]$Name,$Value){$script:f.saved[$Name]=$Value}
function Read-Bc2NativeRunOutcome([string]$Path,[uint32]$ExpectedPid){return Read-ActualNativeOutcome $script:f.bootstrapPath $ExpectedPid}
Check ($RunDirectories.Count -eq $NativeDirectories.Count -and $RunDirectories.Count -gt 0) 'Paired actual run/native evidence'
$null=[IO.Directory]::CreateDirectory($ScratchDirectory)
for($index=0;$index -lt $RunDirectories.Count;$index++){
    $run=$RunDirectories[$index];$nativeFolder=$NativeDirectories[$index]
    $resultPath=Join-Path $run 'result.json';$result=[IO.File]::ReadAllText($resultPath)|ConvertFrom-Json -AsHashtable -Depth 100
    $normal=[IO.File]::ReadAllText((Join-Path $run 'root-normal-close-recovery.json'))|ConvertFrom-Json -AsHashtable
    $restoration=[IO.File]::ReadAllText((Join-Path $run 'root-restoration-recovery.json'))|ConvertFrom-Json -AsHashtable
    $session=[IO.File]::ReadAllText((Join-Path $run 'session-final.json'))|ConvertFrom-Json -AsHashtable -Depth 100
    $nativePath=Join-Path $nativeFolder 'native-trace.json'
    $native=[IO.File]::ReadAllText($nativePath)|ConvertFrom-Json -AsHashtable -Depth 100
    $bootstrapPath=Join-Path $nativeFolder 'native.stdout.log'
    $originalHash=(Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Check ($result.status -ceq 'failed' -and -not $result.native_retirement_proven -and -not $result.reload_success_claimed) 'Original failed claims retained'
    Check ($normal.original_result_sha256 -ceq $originalHash -and $normal.original_failed_result_preserved -and $normal.normal_close_requested -and $normal.game_exited -and -not $normal.forced_termination -and -not $normal.native_retirement_proven) 'Actual normal-close recovery'
    Check ($restoration.original_failure_preserved -and $restoration.exact_three_profiles_restored -and $restoration.game_absent -and -not $restoration.native_retirement_claimed -and @($restoration.profiles|Where-Object {-not $_.verified}).Count -eq 0) 'Actual restoration recovery'
    SameIdentity $normal.exact_game $session.game
    $base=@{result=$result;session=$session;native=$native;bootstrapPath=$bootstrapPath;nativeBytes=(Get-Item -LiteralPath $nativePath).Length;nativeHash=(Get-FileHash -LiteralPath $nativePath -Algorithm SHA256).Hash.ToLowerInvariant()}
    $evidence+=@{run=$run;native_folder=$nativeFolder;result_sha256=$originalHash;native_sha256=$base.nativeHash;native_bytes=$base.nativeBytes;bootstrap_sha256=(Get-FileHash -LiteralPath $bootstrapPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    function RunCase([string]$Name,[scriptblock]$Change,[bool]$AllowClose,[bool]$ExpectRestored=$AllowClose){
        $OutputDir=Join-Path $ScratchDirectory ("actual$index-"+$Name)
        $null=[IO.Directory]::CreateDirectory($OutputDir)
        $script:f=@{identity=$base.session.game.Clone();native=$base.native.Clone();nativeBytes=$base.nativeBytes;nativeHash=$base.nativeHash;bootstrapPath=$base.bootstrapPath;bootstrap=$null;imageHash='original';unowned=$false;unretainedChild=$false;strictScans=0;saved=@{};elapsed=$base.result.elapsed_seconds;closeAccepted=$true;waitExit=$true;exitCode=0;lateIdentity=$false;identityReads=0}
        $owned=@{}
        foreach($row in $base.result.processes){$owned[$row.name]=@{identity=$row.identity;process=[pscustomobject]@{Id=$row.identity.pid;HasExited=$row.exited;ExitCode=$row.exit_code}}}
        $game=$owned.game.process;$game.ExitCode=0
        $game|Add-Member NoteProperty closeCalls 0
        $game|Add-Member NoteProperty waited $null
        $game|Add-Member ScriptMethod CloseMainWindow {$this.closeCalls++;return $script:f.closeAccepted}
        $game|Add-Member ScriptMethod WaitForExit {param($Milliseconds) $this.waited=$Milliseconds;$this.HasExited=$script:f.waitExit;$this.ExitCode=$script:f.exitCode;return $script:f.waitExit}
        $entry=$owned.entry.process;$session=$base.session.Clone();$folder='G:\inert-native-session'
        $gameExe=$owned.game.identity.path;$gamePin=@{sha256='original'};$preAttachSpec=$null;$stopReason='Original stop request'
        $failures=[Collections.Generic.List[string]]::new();foreach($failure in $base.result.failures){$failures.Add($failure)}
        $nativeRetired=$false;$gameClosed=$false;$restored=$false;$caught=$null;$restoreCaught=$null
        $TimeoutSeconds=[int]$base.result.limits.outer_seconds
        $profiles=@()
        foreach($fileIndex in 0..2){
            $originalPath=Join-Path $OutputDir "profiles/profile$fileIndex.bin";$backupPath=Join-Path $OutputDir "backup/profile$fileIndex.bin"
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($originalPath));$null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backupPath))
            [IO.File]::WriteAllBytes($originalPath,[byte[]]@(10,20,$fileIndex))
            [IO.File]::WriteAllBytes($backupPath,[byte[]]@(30,40,$fileIndex))
            $backupPin=Pin $backupPath;$profiles+=@{original=@{path=$originalPath;sha256=$backupPin.sha256};backup=$backupPin}
        }
        & $Change $script:f $owned $session
        if($script:f.bootstrap){
            $script:f.bootstrapPath=Join-Path $OutputDir 'outcome-mutation.json'
            [IO.File]::WriteAllText($script:f.bootstrapPath,($script:f.bootstrap|ConvertTo-Json -Depth 10),$utf8)
        }
        $clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=$script:f.elapsed}}
        try{. $cleanup}catch{$caught=$_.Exception.Message}
        try{. $restore}catch{$restoreCaught=$_.Exception.Message}
        Check (($game.closeCalls -eq 1) -eq $AllowClose) "$Name normal close gate: $caught"
        Check ($restored -eq $ExpectRestored) "$Name restoration gate: $restoreCaught"
        foreach($row in $profiles){Check (((Pin $row.original.path).sha256 -ceq $row.original.sha256) -eq $ExpectRestored) "$Name exact profile bytes"}
        Check (-not $nativeRetired -and -not $script:f.saved.ContainsKey('native-retirement.json')) "$Name no native retirement"
        Check ($failures.Count -ge $base.result.failures.Count) "$Name preserves original failures"
        $receipt=@{reload_success_claimed=$false};$runningObserved=$true;$simObserved=$true;$launcherPreserved=$true
        . $finalStatus
        Check ($receipt.status -ceq 'failed' -and -not $receipt.reload_success_claimed) "$Name remains failed"
        if($AllowClose){
            $recovery=$script:f.saved['native-close-recovery.json']
            Check ($script:f.strictScans -eq 1 -and -not $recovery.hooks_disabled -and -not $recovery.native_retirement_proven -and -not $recovery.reload_flow_drain_claimed -and -not $recovery.resolution_release_confirmed -and $recovery.normal_close_only -and -not $recovery.forced_termination) "$Name truthful recovery"
            if($script:f.closeAccepted){Check ($game.waited -ge 0 -and $game.waited -le 10000 -and $game.waited -le ($TimeoutSeconds-$script:f.elapsed)*1000) "$Name original wait bound"}
        }else{Check ($null -ne $caught -and $null -ne $restoreCaught) "$Name rejects close and restoration"}
        $cases.Add(@{actual_run=$run;case=$Name;close_called=($game.closeCalls -eq 1);restored=$restored;status=$receipt.status;native_retirement_proven=$nativeRetired;cleanup_error=$caught})
    }
    RunCase 'retained-worker' {} $true
    $refusals=[ordered]@{
        live_entry={param($f,$o) $o.entry.process.HasExited=$false;$f.elapsed=529}
        live_supervisor={param($f,$o) $o.supervisor.process.HasExited=$false;$f.elapsed=529}
        live_trace={param($f,$o) $o.trace.process.HasExited=$false}
        live_host={param($f,$o) $o.host.process.HasExited=$false}
        live_resolution={param($f,$o) $o.resolution_helper.process.HasExited=$false}
        live_release={param($f,$o) $o.resolution_release_helper=@{process=[pscustomobject]@{HasExited=$false}}}
        live_setup={param($f,$o) $o.setup_helper=@{process=[pscustomobject]@{HasExited=$false}}}
        trace_exit={param($f,$o) $o.trace.process.ExitCode=1}
        missing_trace={param($f,$o) $o.Remove('trace')}
        reused_game={param($f) $f.identity.start_utc='2026-11-08T00:00:00.0000000Z'}
        changed_path={param($f) $f.identity.path='G:\another\BFBC2Game.exe'}
        changed_image={param($f) $f.imageHash='changed'}
        other_owner={param($f) $f.unowned=$true}
        unretained_child={param($f) $f.unretainedChild=$true}
        expired_deadline={param($f) $f.elapsed=540}
        report_length={param($f) $f.nativeBytes++}
        native_pid={param($f) $f.native.pid++}
        native_string_pid={param($f) $f.native.pid=[string]$f.native.pid}
        native_state={param($f) $f.native.state='failed'}
        session_state={param($f,$o,$s) $s.state='running'}
        resolution_state={param($f,$o,$s) $s.resolution_cleanup='released'}
        resolution_missing_pending={param($f,$o,$s) $s.Remove('resolution_cleanup_pending')}
        resolution_string_pending={param($f,$o,$s) $s.resolution_cleanup_pending='true'}
        resolution_released={param($f,$o,$s) $s.resolution_guard_released=$true}
    }
    foreach($name in $refusals.Keys){RunCase $name $refusals[$name] $false}
    foreach($field in @('module_retained_until_exit','hooks_disabled')){
        foreach($fault in @('opposite','string','missing')){
            RunCase ("native-$field-$fault") ({param($f)
                switch($fault){opposite {$f.native[$field]=-not $f.native[$field]} string {$f.native[$field]=[string]$f.native[$field]} missing {$f.native.Remove($field)}}
            }.GetNewClosure()) $false
        }
    }
    foreach($field in @('module_retained_until_game_exit','remote_thread_exit_observed','report_nonempty','probe_exit','pid','outcome','schema')){
        foreach($fault in @('wrong','missing')){
            RunCase ("bootstrap-$field-$fault") ({param($f)
                $f.bootstrap=[IO.File]::ReadAllText($f.bootstrapPath)|ConvertFrom-Json -AsHashtable
                if($fault -ceq 'missing'){$f.bootstrap.Remove($field)}
                elseif($f.bootstrap[$field] -is [bool]){$f.bootstrap[$field]='true'}
                elseif($f.bootstrap[$field] -is [long] -or $f.bootstrap[$field] -is [int]){$f.bootstrap[$field]++}
                else{$f.bootstrap[$field]='unknown'}
            }.GetNewClosure()) $false
        }
    }
    RunCase 'close_refused' {param($f) $f.closeAccepted=$false} $true $false
    RunCase 'close_timeout' {param($f) $f.waitExit=$false;$f.elapsed=538} $true $false
    RunCase 'nonzero_exit' {param($f) $f.exitCode=1} $true $true
    Check ((Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $originalHash) 'Original result unchanged after replay'
}
# Exercise the production retained-only scan: any unknown live process refuses
# without using the normal startup parentage grace period or any native API.
$owned=@{game=@{identity=$base.session.game;process=[pscustomobject]@{HasExited=$false}}}
$script:f.identity=$base.session.game.Clone()
$script:scanProcess=[pscustomobject]@{Id=$script:f.identity.pid;HasExited=$false;disposed=$false}
$script:scanProcess|Add-Member ScriptMethod Dispose {$this.disposed=$true}
function Get-Process {return $script:scanProcess}
$caught=$null
try{& $productionScan -RetainedOnly}catch{$caught=$_.Exception.Message}
Check ($null -eq $caught -and $script:scanProcess.disposed) 'Retained-only scan accepts exact live game'
foreach($field in @('pid','start_utc','path')){
    $script:f.identity=$base.session.game.Clone();$script:f.identity[$field]=if($field -ceq 'pid'){1}else{'different'}
    $caught=$null;try{& $productionScan -RetainedOnly}catch{$caught=$_.Exception.Message}
    Check ($null -ne $caught -and $caught.StartsWith('Unowned BC2 process detected:')) "Retained-only scan rejects $field mismatch without parent fallback"
}
$summary=@{evidence_level='deterministic_test';passed=$true;checks=$checks;cases=@($cases);actual_evidence=$evidence;game_or_input_actions=$false;source_runner=(Get-FileHash -LiteralPath $RunnerPath -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText((Join-Path $ScratchDirectory 'result.json'),($summary|ConvertTo-Json -Depth 15),$utf8)
Write-Output "Failed-worker cleanup checks passed: $checks; $($cases.Count) actual-evidence replays; no real processes or profiles changed."

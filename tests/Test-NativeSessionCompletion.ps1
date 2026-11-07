#requires -Version 7.5
# Offline: execute the production parsers and session publication functions only.
# No Session.ps1 startup, helper process, game, XR, or installed file is mutated.
param(
    [string]$RepositoryRoot=(Split-Path $PSScriptRoot -Parent),
    [string]$RuntimeSourceRoot='',
    [string]$WorkRoot='',
    [string]$SavedSession=''
)
$ErrorActionPreference='Stop'
if(-not $RuntimeSourceRoot){$RuntimeSourceRoot=Join-Path $RepositoryRoot 'launcher/runtime'}
if(-not $WorkRoot){$WorkRoot=Join-Path $RepositoryRoot ('test-temp/native-completion-'+[guid]::NewGuid().ToString('N'))}
$null=[IO.Directory]::CreateDirectory($WorkRoot)
$savedEnvironment=@{};foreach($key in @('TEMP','TMP','TMPDIR')){$savedEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$WorkRoot,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Save-Text([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Snapshot([string]$Path){$item=Get-Item -LiteralPath $Path;return @($item.Length,$item.LastWriteTimeUtc.Ticks,(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash) -join ':'}
try {
    . (Join-Path $RepositoryRoot 'launcher/Launcher.Core.ps1')
    . (Join-Path $RuntimeSourceRoot 'Startup.ps1')
    $tokens=$null;$parseErrors=$null
    $sessionAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RuntimeSourceRoot 'Session.ps1'),[ref]$tokens,[ref]$parseErrors)
    Check ($parseErrors.Count -eq 0) 'session source parses'
    foreach($name in @('Save-NativeOutcome','Save-NativeReportCompletion')){
        $functions=@($sessionAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$false))
        Check ($functions.Count -eq 1) ("production function found: $name")
        . ([scriptblock]::Create($functions[0].Extent.Text))
    }
    $completionCall=@($sessionAst.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Save-NativeReportCompletion'},$true))
    $outerTry=@($sessionAst.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})[-1]
    Check ($completionCall.Count -eq 1 -and $completionCall[0].Extent.StartOffset -gt $outerTry.Finally.Extent.StartOffset -and $completionCall[0].Extent.EndOffset -lt $outerTry.Finally.Extent.EndOffset) 'all final session outcomes publish report completion in finally'
    Check ($outerTry.Finally.Extent.Text.IndexOf('Save-NativeReportCompletion') -lt $outerTry.Finally.Extent.Text.IndexOf('$state.ended_utc=')) 'completion fields precede final session save'

    $folder=Join-Path $WorkRoot 'fixture';$null=[IO.Directory]::CreateDirectory($folder)
    $report=Join-Path $folder 'native-trace.json'
    # Exact saved human74 native stdout. The final report was zero bytes.
    $human74='{"schema":"bc2vr.native-run-outcome.v1","pid":61408,"outcome":"remote_thread_exited","remote_thread_exit_observed":true,"probe_exit":0,"game_exit_code":null,"report_bytes":0,"report_nonempty":false,"module_retained_until_game_exit":true,"game_files_modified":false}'
    Save-Text (Join-Path $folder 'native.stdout.log') ($human74+"`r`n")
    Save-Text (Join-Path $folder 'session-telemetry.jsonl') "{`"input_samples`":10}`n"
    Save-Text (Join-Path $folder 'stereo-progress.jsonl') "{`"consumed_pairs`":9}`n"
    $telemetryBefore=Snapshot (Join-Path $folder 'session-telemetry.jsonl')
    $stereoBefore=Snapshot (Join-Path $folder 'stereo-progress.jsonl')
    $outcomeBefore=Snapshot (Join-Path $folder 'native.stdout.log')
    $trace=[pscustomobject]@{HasExited=$true;ExitCode=0}
    function New-State {return @{state='ended';phase='VR connected; native VR input received.';error=$null;game=@{pid=61408};native_exit_code=0;resolution_cleanup='game_exited'}}
    function Observe([string]$Expected,[bool]$Complete){
        $result=Save-NativeOutcome
        Save-NativeReportCompletion
        Check ($state.native_report_status -ceq $Expected -and $state.native_report_completed -is [bool] -and $state.native_report_completed -eq $Complete -and $state.native_hooks_disabled_observed -is [bool] -and $state.native_hooks_disabled_observed -eq $Complete -and -not $state.ContainsKey('native_cleanup_observed')) ("typed report and hooks-disabled evidence: $Expected")
        Check ($state.state -ceq 'ended' -and $null -eq $state.error -and $state.native_exit_code -eq 0 -and $state.native_outcome -ceq 'remote_thread_exited' -and $state.native_remote_thread_exit_observed -and $state.native_probe_exit -eq 0 -and $null -eq $state.native_game_exit_code -and $state.native_report_bytes -eq 0 -and -not $state.native_report_nonempty -and $state.resolution_cleanup -ceq 'game_exited') ("original human74 exit outcome and session state preserved: $Expected")
    }
    $state=New-State;Observe 'missing' $false
    Save-Text $report ''; $state=New-State;Observe 'empty' $false
    Check ($state.native_report_verified_bytes -eq 0 -and $state.phase.Contains('final diagnostic report is unavailable')) 'human74 zero exit with zero-byte report cannot claim completion'

    $cases=@(
        @{name='valid final report';text='{"state":"observed","pid":61408,"hooks_disabled":true,"gameplay":{"reload_flow":{"drained":true}}}';status='complete';complete=$true},
        @{name='wrong PID';text='{"state":"observed","pid":61409,"hooks_disabled":true}';status='pid_mismatch';complete=$false},
        @{name='malformed JSON';text='not JSON';status='malformed';complete=$false},
        @{name='truncated JSON';text='{"state":"observed","pid":61408,"hooks_disabled":true';status='malformed';complete=$false},
        @{name='missing hook result';text='{"state":"observed","pid":61408}';status='incomplete';complete=$false},
        @{name='hooks still active';text='{"state":"observed","pid":61408,"hooks_disabled":false}';status='incomplete';complete=$false},
        @{name='string hooks';text='{"pid":61408,"hooks_disabled":"true"}';status='malformed';complete=$false},
        @{name='string PID';text='{"pid":"61408","hooks_disabled":true}';status='malformed';complete=$false},
        @{name='missing PID';text='{"state":"failed","hooks_disabled":true}';status='incomplete';complete=$false},
        @{name='array wrapper';text='[{"pid":61408,"hooks_disabled":true}]';status='malformed';complete=$false}
    )
    foreach($case in $cases){
        Save-Text $report $case.text;$before=Snapshot $report;$state=New-State
        Observe $case.status $case.complete
        Check ((Snapshot $report) -ceq $before) ($case.name+' report is retained byte-for-byte')
    }
    [IO.File]::WriteAllBytes($report,[byte[]]@(123,255,125));$state=New-State;Observe 'malformed' $false
    Save-Text $report '{"pid":61408,"hooks_disabled":true}'
    $writer=[IO.File]::Open($report,'Open','ReadWrite',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {$state=New-State;Observe 'complete' $true}finally{$writer.Dispose()}
    Check ($state.phase -ceq 'VR session ended; final diagnostics saved.') 'verified report completion gets a clear final phase'
    $state=New-State;$state.state='failed';$state.error='Original native helper failure';$state.native_exit_code=2;$phase=$state.phase
    Save-NativeReportCompletion
    Check ($state.native_report_completed -and $state.native_hooks_disabled_observed -and $state.state -ceq 'failed' -and $state.error -ceq 'Original native helper failure' -and $state.native_exit_code -eq 2 -and $state.phase -ceq $phase) 'valid report never converts an original failure into success'
    $state=New-State;$state.state='stopped';$state.native_drained=$true;Save-Text $report ''
    Save-NativeReportCompletion
    Check ($state.state -ceq 'stopped' -and $state.native_drained -and $state.native_bootstrap_exited -and -not $state.native_report_completed -and -not $state.native_hooks_disabled_observed) 'existing helper-wait field is preserved while stopped-session report remains unobserved'
    $state=New-State;$trace.HasExited=$false;Save-Text $report '{"pid":61408,"hooks_disabled":true}'
    Save-NativeReportCompletion
    Check ($state.native_report_status -ceq 'pending' -and -not $state.native_bootstrap_exited -and -not $state.native_report_completed) 'live helper does not publish completion from a report'
    $state=New-State;$trace=$null;Save-NativeReportCompletion
    Check (-not $state.ContainsKey('native_report_status')) 'no native attachment creates no completion claim'
    Check ((Snapshot (Join-Path $folder 'session-telemetry.jsonl')) -ceq $telemetryBefore -and (Snapshot (Join-Path $folder 'stereo-progress.jsonl')) -ceq $stereoBefore -and (Snapshot (Join-Path $folder 'native.stdout.log')) -ceq $outcomeBefore) 'streaming evidence and original native outcome remain byte-for-byte intact'

    if($SavedSession){
        # Optional replay reads the retained actual-session artifacts directly;
        # all state mutation below is an in-memory deserialized session only.
        $folder=$SavedSession;$report=Join-Path $folder 'native-trace.json'
        $savedStatePath=Join-Path $folder 'session.json';$savedOutcomePath=Join-Path $folder 'native.stdout.log'
        $stateBefore=Snapshot $savedStatePath;$reportBefore=Snapshot $report;$nativeBefore=Snapshot $savedOutcomePath
        $streamInfo=Get-Item -LiteralPath (Join-Path $folder 'session-telemetry.jsonl')
        $streamLength=$streamInfo.Length;$streamTicks=$streamInfo.LastWriteTimeUtc.Ticks
        $state=Read-FvrJson $savedStatePath;$trace=[pscustomobject]@{HasExited=$true;ExitCode=0}
        Check ($state.game.pid -eq 61408 -and [IO.File]::ReadAllText($savedOutcomePath).Trim() -ceq $human74 -and (Get-Item -LiteralPath $report).Length -eq 0) 'retained human74 fixture is the exact reported failure'
        Observe 'empty' $false
        $streamAfter=Get-Item -LiteralPath (Join-Path $folder 'session-telemetry.jsonl')
        Check ((Snapshot $savedStatePath) -ceq $stateBefore -and (Snapshot $report) -ceq $reportBefore -and (Snapshot $savedOutcomePath) -ceq $nativeBefore -and $streamAfter.Length -eq $streamLength -and $streamAfter.LastWriteTimeUtc.Ticks -eq $streamTicks) 'retained actual session and its streaming evidence are untouched'
    }
    Write-Host "Native session completion checks passed: $script:checks"
}finally{
    foreach($key in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedEnvironment[$key],'Process')}
}

param([string]$Candidate=(Split-Path $PSScriptRoot -Parent),[string]$EvidenceRoot=$env:TEMP)
$ErrorActionPreference='Stop'
$source=Join-Path $Candidate 'Start-BC2VRSession.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Candidate does not parse'}
$fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Stop-BC2OwnedXrHost'},$true)
if(-not $fn){throw 'Missing production cleanup function'}
. ([scriptblock]::Create($fn.Extent.Text))
$evidence=Join-Path $EvidenceRoot ('bc2vr-host-shutdown-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $evidence
$fixture=Join-Path $evidence 'host-fixture.ps1'
@'
param([string]$StopFile,[string]$Report,[string]$Mode)
if($Mode -eq 'exit'){exit 0}
[IO.File]::WriteAllText($Report+'.ready','ready')
while($true){
    if($Mode -eq 'respond' -and (Test-Path -LiteralPath $StopFile)){
        [IO.File]::WriteAllText($Report,'{"flushed":true}');exit 0
    }
    Start-Sleep -Milliseconds 20
}
'@ | Set-Content -LiteralPath $fixture
$processes=[Collections.Generic.List[Diagnostics.Process]]::new()
$rows=[Collections.Generic.List[object]]::new()
function Launch([string]$Name,[string]$Mode){
    $stop=Join-Path $evidence ($Name+'.stop');$report=Join-Path $evidence ($Name+'.json')
    $exe=(Get-Process -Id $PID).Path
    $child=Start-Process -FilePath $exe -ArgumentList @('-NoProfile','-File',('"'+$fixture+'"'),'-StopFile',('"'+$stop+'"'),'-Report',('"'+$report+'"'),'-Mode',$Mode) -WindowStyle Hidden -PassThru
    $null=$child.Handle;$processes.Add($child)
    if($Mode -ne 'exit'){
        $watch=[Diagnostics.Stopwatch]::StartNew()
        while(-not (Test-Path -LiteralPath ($report+'.ready')) -and $watch.ElapsedMilliseconds -lt 5000){Start-Sleep -Milliseconds 20}
        if(-not (Test-Path -LiteralPath ($report+'.ready'))){throw 'Fixture not ready'}
    }
    [pscustomobject]@{process=$child;stop=$stop;report=$report}
}
try {
    $good=Launch 'graceful' 'respond'
    $row=Stop-BC2OwnedXrHost -Process $good.process -StopFile $good.stop -WaitMilliseconds 1500
    if(-not $row.graceful -or $row.forced -or -not $row.exited -or -not $row.stop_requested){throw 'Graceful shutdown failed'}
    if(-not (Get-Content -Raw -LiteralPath $good.report | ConvertFrom-Json).flushed){throw 'Host report was lost'}
    $rows.Add([pscustomobject]@{case='graceful_flush';result=$row})
    $finished=Launch 'finished' 'exit';$null=$finished.process.WaitForExit(5000)
    $row=Stop-BC2OwnedXrHost -Process $finished.process -StopFile $finished.stop -WaitMilliseconds 100
    if(-not $row.exited -or $row.forced -or $row.stop_requested -or (Test-Path -LiteralPath $finished.stop)){throw 'Exited process cleanup changed state'}
    $rows.Add([pscustomobject]@{case='already_exited';result=$row})
    $sentinel=Launch 'unrelated' 'ignore';$hung=Launch 'hung' 'ignore'
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $row=Stop-BC2OwnedXrHost -Process $hung.process -StopFile $hung.stop -WaitMilliseconds 100
    if($row.graceful -or -not $row.forced -or -not $row.exited -or $sentinel.process.HasExited -or $watch.ElapsedMilliseconds -gt 4000){throw 'Bounded owned-host fallback failed'}
    $rows.Add([pscustomobject]@{case='bounded_exact_process_fallback';result=$row})
    $badPath=Launch 'badpath' 'ignore'
    $row=Stop-BC2OwnedXrHost -Process $badPath.process -StopFile (Join-Path $evidence 'missing/stop') -WaitMilliseconds 100
    if($row.stop_requested -or -not $row.forced -or -not $row.exited -or -not $row.error -or $sentinel.process.HasExited){throw 'Stop-file error fallback failed'}
    $rows.Add([pscustomobject]@{case='stop_file_error';result=$row})
    $caught=Launch 'actual-catch' 'respond';$xr=$caught.process;$stopFile=$caught.stop;$state=[ordered]@{state='running';error=$null}
    $tryNode=$ast.Find({param($node) $node -is [Management.Automation.Language.TryStatementAst] -and $node.Extent.Text.Contains('$state.state=''failed''')},$false)
    if(-not $tryNode){throw 'Production failure path not found'}
    $failureText=($tryNode.CatchClauses[0].Body.Statements | ForEach-Object {$_.Extent.Text}) -join "`n"
    $failureBody=[scriptblock]::Create("try {throw 'original native failure fixture'} catch {`n"+$failureText+"`n}")
    $originalPreserved=$false
    try {& $failureBody} catch {$caughtMessage=$_.Exception.Message;$originalPreserved=$caughtMessage -eq 'original native failure fixture'}
    if(-not $originalPreserved -or $state.state -ne 'failed' -or -not $state.host_shutdown.graceful -or -not (Test-Path -LiteralPath $caught.report)){
        [ordered]@{message=$caughtMessage;state=$state;body=$failureBody.ToString()} | ConvertTo-Json -Depth 6 | Write-Output
        throw 'Production catch lost original error or diagnostics'
    }
    $rows.Add([pscustomobject]@{case='actual_catch_preserves_failure_and_flushes';result=$state.host_shutdown})
    $rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidence 'results.json')
    Write-Output ('5 owned-host shutdown cases passed: '+$evidence)
} finally {
    foreach($child in $processes){if(-not $child.HasExited){$child.Kill();$null=$child.WaitForExit(2000)};$child.Dispose()}
}

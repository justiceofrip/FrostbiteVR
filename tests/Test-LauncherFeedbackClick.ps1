#requires -Version 7.5
param([string]$LauncherPath,[switch]$BaselineExpectFailure)
# Exercise the actual dialog tail in a file/function-local scope. No WPF,
# browser, network, child process, game or desktop input is used by this test.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
if(-not $LauncherPath){$LauncherPath=Join-Path $repo 'launcher/Launcher.ps1'}
$LauncherPath=[IO.Path]::GetFullPath($LauncherPath)
$root=Join-Path $repo ('test-temp/feedback-click-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$passed=$false;$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Invoke-FeedbackClickFixture([string]$Choice,[string]$DialogTail) {
    # This function deliberately owns Begin-Action locally, as Launcher.ps1
    # owns it in its file scope. A new closure's dynamic module must not be
    # allowed to accidentally find a global helper and conceal the old bug.
    $script:dispatchCalls=0;$script:job=$null;$script:jobAction=$null
    function Begin-Action([string]$Action) {
        $script:dispatchCalls++
        $script:jobAction=$Action
        $script:job=[pscustomobject]@{owner='outer-file-scope';action=$Action}
    }
    $button=[pscustomobject]@{Callback=$null;Invocations=0}
    $button|Add-Member ScriptMethod Add_Click {
        param([scriptblock]$Handler)
        if($null -ne $this.Callback){throw 'Duplicate SaveBundle registration'}
        $this.Callback=$Handler
    }
    $dialog=[pscustomobject]@{Tag=$null;Closed=$false;Choice=$Choice;SaveButton=$button;Shows=0}
    $dialog|Add-Member ScriptMethod FindName {
        param([string]$Name)
        if($Name -cne 'SaveBundle'){throw 'Unexpected dialog control in extracted save wiring'}
        return $this.SaveButton
    }
    $dialog|Add-Member ScriptMethod Close {$this.Closed=$true}
    $dialog|Add-Member ScriptMethod ShowDialog {
        $this.Shows++
        if($this.Choice -ceq 'Save'){
            if($null -eq $this.SaveButton.Callback){throw 'SaveBundle handler was not registered'}
            $this.SaveButton.Invocations++
            & $this.SaveButton.Callback
        }else{$this.Close()}
        return $null
    }
    $failure=$null
    # Match the UI's local dialog variable at registration; an extra child
    # scope would keep GetNewClosure from capturing it and test a fake failure.
    try {. ([scriptblock]::Create($DialogTail))}catch{$failure=$_}
    $message=if($failure){[string]$failure.Exception.Message}else{''}
    return @{
        choice=$Choice;shows=$dialog.Shows;closed=$dialog.Closed;tag=$dialog.Tag
        click_invocations=$button.Invocations;dispatch_calls=$script:dispatchCalls
        job_action=$script:jobAction;job_owner=$(if($script:job){$script:job.owner}else{$null})
        failed=($null -ne $failure)
        error_id=$(if($failure){[string]$failure.FullyQualifiedErrorId}else{$null})
        error=$message.Substring(0,[Math]::Min(1200,$message.Length))
    }
}
try {
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $root -Operation Session -PlannedBytes 256KB -RawCaptureBytes 0
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage admission required'}
    $null=Set-FvrLauncherStorage $root
    Write-Host ('Owned fixture evidence: '+$root)
    Assert-FvrNoLinks $LauncherPath
    if((Get-Item -LiteralPath $LauncherPath).Length -gt 256KB){throw 'Unexpected oversized launcher source'}
    $source=[IO.File]::ReadAllText($LauncherPath)
    $tokens=$null;$parseErrors=$null
    $ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$parseErrors)
    Check ($parseErrors.Count -eq 0) 'actual launcher source parses'
    $registrations=@($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
        $node.Member.Value -ceq 'Add_Click' -and
        $node.Expression -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
        $node.Expression.Member.Value -ceq 'FindName' -and
        $node.Expression.Arguments.Count -eq 1 -and
        $node.Expression.Arguments[0] -is [Management.Automation.Language.StringConstantExpressionAst] -and
        $node.Expression.Arguments[0].Value -ceq 'SaveBundle'
    },$true))
    Check ($registrations.Count -eq 1) 'exactly one actual SaveBundle click registration is located'
    $enclosing=$registrations[0].Parent
    while($enclosing -and $enclosing -isnot [Management.Automation.Language.TryStatementAst]){$enclosing=$enclosing.Parent}
    Check ($null -ne $enclosing) 'save registration belongs to the feedback dialog try block'
    $statements=@($enclosing.Body.Statements)
    $start=@($statements|Where-Object {$_.Extent.StartOffset -le $registrations[0].Extent.StartOffset -and $_.Extent.EndOffset -ge $registrations[0].Extent.EndOffset})
    Check ($start.Count -eq 1) 'actual registration statement has an unambiguous source extent'
    $tailStatements=@($statements|Where-Object {$_.Extent.StartOffset -ge $start[0].Extent.StartOffset})
    $tail=$source.Substring($start[0].Extent.StartOffset,$tailStatements[-1].Extent.EndOffset-$start[0].Extent.StartOffset)
    Check ($tail.Length -le 8192 -and $tail.Contains('$dialog.ShowDialog()')) 'bounded actual dialog tail includes modal return and any following dispatch'
    # Refuse an ambient helper: this would make the old dynamic-module lookup
    # succeed accidentally and invalidate the file-local regression.
    if(Get-Command Begin-Action -ErrorAction SilentlyContinue){throw 'Run this fixture in a clean -File scope without an ambient Begin-Action function'}
    [IO.File]::WriteAllText((Join-Path $root 'dialog-tail.ps1'),$tail,[Text.UTF8Encoding]::new($false))
    $saveResult=Invoke-FeedbackClickFixture 'Save' $tail
    $dismissResult=Invoke-FeedbackClickFixture 'Dismiss' $tail
    Write-FvrJson (Join-Path $root 'result.json') @{
        schema='bc2vr.feedback-click-fixture.v1';launcher_sha256=(Get-FileHash -LiteralPath $LauncherPath -Algorithm SHA256).Hash.ToLowerInvariant()
        baseline_expected_failure=[bool]$BaselineExpectFailure;save=$saveResult;dismiss=$dismissResult
        visible_ui=$false;worker_started=$false;game_started=$false;network_used=$false
    }
    Check ($saveResult.shows -eq 1 -and $saveResult.closed -and $saveResult.click_invocations -eq 1) 'Save invokes the actual registered callback once and closes its dialog'
    if($BaselineExpectFailure){
        Check ($saveResult.failed -and $saveResult.dispatch_calls -eq 0 -and
            ($saveResult.error_id -match 'CommandNotFound' -or $saveResult.error.Contains('Begin-Action'))) 'old closure cannot resolve file-local Begin-Action and dispatches no worker'
        Check ($null -eq $saveResult.job_owner -and $null -eq $saveResult.job_action) 'old failing closure leaves outer script job ownership unchanged'
    }else{
        Check (-not $saveResult.failed -and $saveResult.dispatch_calls -eq 1 -and $saveResult.job_action -ceq 'Feedback') 'Save reaches file-local Begin-Action exactly once with Feedback'
        Check ($saveResult.job_owner -ceq 'outer-file-scope') 'feedback dispatch retains the outer script job ownership'
    }
    Check (-not $dismissResult.failed -and $dismissResult.shows -eq 1 -and $dismissResult.closed -and
        $dismissResult.click_invocations -eq 0 -and $dismissResult.dispatch_calls -eq 0 -and
        $null -eq $dismissResult.job_action -and $null -eq $dismissResult.job_owner) 'dismissing the dialog never dispatches Feedback'
    $passed=$true
    Write-Host "Feedback click checks passed: $script:checks; no visible UI, child, game or network activity."
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if($passed){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}
    else{Write-Warning ('Fixture evidence retained: '+$root)}
}

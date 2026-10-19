#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/Launcher.Feedback.ps1')
. (Join-Path $repo 'launcher/runtime/Startup.ps1')
$root=Join-Path $repo ('test-temp/native-failure-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
try {
    $null=Set-FvrLauncherStorage $root
    $runtime=Join-Path $root 'data/runtime';$run=Join-Path $runtime 'sessions/test-one'
    $null=[IO.Directory]::CreateDirectory($run)
    $report=Join-Path $run 'native-trace.json';$stderr=Join-Path $run 'native.stderr.log'
    $fallback='The native VR adapter exited during startup.'
    # The tester's 0.1.7 stdout first exercises missing-report fallback, then
    # the exact 113-byte failure report supplied in runtime.zip supplies cause.
    [IO.File]::WriteAllText((Join-Path $run 'native.stdout.log'),'{"pid":32580,"probe_exit":20,"module_retained_until_game_exit":true,"game_files_modified":false}')
    $detail=Get-Bc2StartupFailure $run 'native' $fallback
    Check ($detail.Contains('32580') -and $detail.Contains('"probe_exit":20')) 'missing native report preserves exact tester stdout fallback'
    [IO.File]::WriteAllText($report,('{"state":"failed","error":"Mechanism audio callback binding or private cache unverified","hooks_disabled":true}'+"`r`n"),[Text.UTF8Encoding]::new($false))
    $detail=Get-Bc2StartupFailure $run 'native' $fallback
    Check ((Get-Item -LiteralPath $report).Length -eq 113 -and (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash -eq '04e37a29c3063bd8ae2b1772fad7deb5c9e36a154ec81669e1e8d68af52b6e47' -and $detail.Contains('Mechanism audio callback binding or private cache unverified') -and -not $detail.Contains('probe_exit') -and $detail.Contains('Report feedback')) 'exact tester native failure supplies concise underlying cause instead of code20'
    $summary=Read-FvrNativeFailureFeedback $report|ConvertFrom-Json -AsHashtable
    Check ($summary.error -eq 'Mechanism audio callback binding or private cache unverified' -and $summary.hooks_disabled -eq $true -and $summary.Count -eq 3) 'feedback includes only actual tester failure reason and typed cleanup result'
    [IO.File]::WriteAllText($stderr,'Existing native stderr detail')
    $invalid=@('', '{"state":"failed","error":"unfinished', 'NOT-JSON-MUST-NOT-LEAK', ('x'*65537), '{"state":"observed","error":"NOT-A-FAILURE"}', '{"state":"failed","error":["NOT-A-STRING"]}', '"NOT-AN-OBJECT"')
    foreach($text in $invalid){
        [IO.File]::WriteAllText($report,$text)
        $detail=Get-Bc2StartupFailure $run 'native' $fallback
        Check ($detail.Contains('Existing native stderr detail') -and -not $detail.Contains('NOT-A-FAILURE') -and $detail.Length -lt 2000) 'invalid, partial, large or nonfailure trace retains existing stderr fallback'
        $summary=Read-FvrNativeFailureFeedback $report
        Check ($summary.Length -lt 200 -and -not $summary.Contains('MUST-NOT-LEAK') -and -not $summary.Contains('NOT-A-FAILURE')) 'unusable trace produces a bounded omission notice without raw content'
    }
    [IO.File]::WriteAllBytes($report,[byte[]]@(0xC3,0x28))
    Check ((Get-Bc2StartupFailure $run 'native' $fallback).Contains('Existing native stderr detail') -and (Read-FvrNativeFailureFeedback $report).Contains('omitted')) 'invalid UTF8 cannot replace native log fallback'
    $literal='$([IO.File]::WriteAllText("SHOULD-NOT-EXECUTE","bad"))'
    Write-FvrJson $report @{state='failed';error=("Native guard: "+$literal+[char]27+"`n"+('x'*6000));hooks_disabled='not a boolean'}
    $detail=Get-Bc2StartupFailure $run 'native' $fallback
    $summary=Read-FvrNativeFailureFeedback $report|ConvertFrom-Json -AsHashtable
    Check ($detail.Contains($literal) -and $detail.Length -lt 1800 -and -not $detail.Contains([string][char]27)) 'native error remains literal bounded text with control characters removed'
    Check ($summary.error.Length -le 1600 -and -not $summary.ContainsKey('hooks_disabled')) 'long failure is capped and untyped cleanup evidence omitted'
    Check (-not(Test-Path -LiteralPath (Join-Path (Get-Location) 'SHOULD-NOT-EXECUTE'))) 'native error text is never evaluated'
    Write-FvrJson $report @{state='failed';error="Native gameplay binding unverified`nG:\Private Person\install\file`ntoken=private-secret`nprivate@example.test";hooks_disabled=$false;extra='MUST-NOT-LEAK-EXTRA'}
    Write-FvrJson (Join-Path $runtime 'active-session.json') @{relative_folder='sessions/test-one'}
    $package=@{version='0.1.7';runtime_entry='runtime/Entry.ps1';release_repository='justiceofrip/BC2VR';feedback_repository='justiceofrip/BC2VR'}
    $settings=@{local_backend=$null;game_path='G:\Private Person\game'}
    $bundle=New-FvrFeedbackBundle $root $package $settings
    $text=[IO.File]::ReadAllText($bundle.report)
    Check ($text.Contains('native-trace.json (bounded failure summary, sanitized)') -and $text.Contains('Native gameplay binding unverified')) 'actual local feedback bundle contains the underlying native failure'
    Check (-not $text.Contains('Private Person') -and -not $text.Contains('private-secret') -and -not $text.Contains('private@example.test') -and -not $text.Contains('MUST-NOT-LEAK')) 'native failure follows existing path credential email and field privacy policy'
    Check ($bundle.uploaded -eq $false -and $bundle.bytes -lt 512KB) 'failure diagnostics remain bounded and local'
    $outside=Join-Path $root 'outside';$null=[IO.Directory]::CreateDirectory($outside)
    Write-FvrJson (Join-Path $outside 'native-trace.json') @{state='failed';error='MUST-NOT-READ-LINK'}
    $linked=Join-Path $runtime 'sessions/linked';$null=New-Item -ItemType Junction -Path $linked -Target $outside
    Check ($null -eq (Read-Bc2NativeFailureError (Join-Path $linked 'native-trace.json')) -and -not(Read-FvrNativeFailureFeedback (Join-Path $linked 'native-trace.json')).Contains('MUST-NOT-READ')) 'linked native report is never read'
    Remove-Item -LiteralPath $linked
    Write-Host "Native failure diagnostics checks passed: $script:checks; no game, XR, network or native attach."
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}

#requires -Version 7.5
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ScratchRoot,
    [string]$RunnerPath=(Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'The unchanged runner entry requires Windows.'}
if(-not [IO.Path]::IsPathFullyQualified($ScratchRoot) -or [IO.Path]::GetPathRoot($ScratchRoot) -ine 'G:\'){throw 'Use an absolute G: scratch directory.'}
$ScratchRoot=[IO.Path]::GetFullPath($ScratchRoot)
$utf8=[Text.UTF8Encoding]::new($false)
$checks=[Collections.Generic.List[string]]::new()
$cases=[Collections.Generic.List[object]]::new()
function Check([bool]$Good,[string]$Name){if(-not $Good){throw ('FAILED: '+$Name)};$checks.Add($Name)}
function Parse([string]$Path){
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) ('Parse '+$Path)
    return $ast
}
$runnerAst=Parse $RunnerPath
$storagePath=Join-Path (Split-Path $RunnerPath -Parent) 'StorageBudget.ps1'
$storageAst=Parse $storagePath
$guard=$storageAst.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Assert-FvrStorageBudget'},$true)
Check ($null -ne $guard) 'actual guard parameter contract found'
$calls=@($runnerAst.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Assert-FvrStorageBudget'},$true))
Check ($calls.Count -eq 1) 'runner retains one storage admission call'

# Execute an exact copy of the entire entry script in an isolated runspace.
# Only imported dependencies are synthetic. No real inventory, process discovery,
# game files, runtime or launcher entry is available in this fixture.
$fixture=Join-Path $ScratchRoot ('explicit-budget-'+[guid]::NewGuid().ToString('N'))
foreach($relative in @('tools','launcher/runtime','reports')){$null=[IO.Directory]::CreateDirectory((Join-Path $fixture $relative))}
$runnerCopy=Join-Path $fixture 'tools/Invoke-Bc2XrGameSimulation.ps1'
[IO.File]::Copy([IO.Path]::GetFullPath($RunnerPath),$runnerCopy,$false)
Check ((Get-FileHash $RunnerPath).Hash -ceq (Get-FileHash $runnerCopy).Hash) 'entire executed runner matches source bytes'
$launcherStub=@'
function Write-FvrJson($Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
function Get-Process {param($Name,$ErrorAction) $global:XrBudgetFixture.process_discovery_calls++}
function Assert-FvrVersion {param($Version) throw 'SYNTHETIC_STOP_AFTER_ADMISSION_MANIFEST'}
function Start-Process {throw 'SYNTHETIC_PROCESS_START_FORBIDDEN'}
function Stop-Process {throw 'SYNTHETIC_PROCESS_STOP_FORBIDDEN'}
'@
[IO.File]::WriteAllText((Join-Path $fixture 'launcher/Launcher.Core.ps1'),$launcherStub,$utf8)
foreach($name in @('Runtime.Core.ps1','Startup.ps1','Resolution.ps1')){
    [IO.File]::WriteAllText((Join-Path $fixture ('launcher/runtime/'+$name)),'# Synthetic dependency; intentionally empty.',$utf8)
}
# Use the actual guard's binding/validation/default contract, but never its scan.
$guardBody=@'
    $bound=@{};foreach($key in $PSBoundParameters.Keys){$bound[$key]=$PSBoundParameters[$key]}
    $global:XrBudgetFixture.calls.Add(@{bound=$bound;min_free_bytes=$MinFreeBytes;max_generated_bytes=$MaxGeneratedBytes})
    if($global:XrBudgetFixture.refuse){throw 'SYNTHETIC_FRESH_STORAGE_REFUSAL'}
    return [pscustomobject]@{schema='synthetic.storage-boundary.v1';max_generated_bytes=$MaxGeneratedBytes;min_free_bytes=$MinFreeBytes;planned_bytes=$PlannedBytes;raw_capture_bytes=$RawCaptureBytes;max_raw_capture_bytes=$MaxRawCaptureBytes}
'@
$storageStub="function Assert-FvrNoLinks {param([string]`$Path)}`nfunction Assert-FvrStorageBudget {`n[CmdletBinding()]`n"+$guard.Body.ParamBlock.Extent.Text+"`n"+$guardBody+"`n}`n"
[IO.File]::WriteAllText((Join-Path $fixture 'tools/StorageBudget.ps1'),$storageStub,$utf8)

function Run([string]$Name,[hashtable]$Extra,[string]$Outcome,[long]$ExpectedCeiling=40GB,[string]$ExpectedReason='', [bool]$Refuse=$false){
    $output=Join-Path $fixture ('reports/'+$Name)
    $state=@{calls=[Collections.Generic.List[object]]::new();process_discovery_calls=0;refuse=$Refuse}
    $arguments=@{RepositoryRoot=$fixture;InstallRoot=(Join-Path $fixture 'synthetic-install');Version='0.1.83';OutputDir=$output;LocalConfig=(Join-Path $fixture 'synthetic-config.json');ExpectedSourceSha256=('0'*64);ProfileDirectory=(Join-Path $fixture 'synthetic-profiles')}
    foreach($key in $Extra.Keys){$arguments[$key]=$Extra[$key]}
    $ps=[powershell]::Create()
    try{
        $ps.Runspace.SessionStateProxy.SetVariable('XrBudgetFixture',$state)
        $null=$ps.AddCommand($runnerCopy).AddParameters($arguments)
        $invocationFailure=$null
        try{$null=$ps.Invoke()}catch{$invocationFailure=$_.Exception.Message}
        $errors=@($ps.Streams.Error|ForEach-Object {$_.ToString()})
        if($invocationFailure){$errors+=,$invocationFailure}
    }finally{$ps.Dispose()}
    $refusals=@(Get-ChildItem -LiteralPath (Join-Path $fixture 'reports') -Filter ($Name+'.refused-*.json') -File)
    if($Outcome -ceq 'invalid_size'){
        Check ($errors.Count -gt 0 -and ($errors -join ' ') -match 'MaxGeneratedBytes') ($Name+' rejects invalid size at parameter binding')
        Check ($state.calls.Count -eq 0 -and -not [IO.Directory]::Exists($output) -and $refusals.Count -eq 0) ($Name+' cannot reach guard or create output')
    }elseif($Outcome -ceq 'capture_refusal'){
        $expectedFailure=if($Extra.ContainsKey('CampaignObservation') -and $Extra.CampaignObservation){'Capture plan exceeds the unchanged 660 MiB raw ceiling including native images.'}else{'Capture dimensions/count exceed the explicitly selected bounded profile.'}
        Check ($errors.Count -gt 0 -and ($errors -join ' ').Contains($expectedFailure)) ($Name+' refuses the exact capture admission mismatch')
        Check ($state.calls.Count -eq 0 -and $state.process_discovery_calls -eq 0 -and -not [IO.Directory]::Exists($output) -and $refusals.Count -eq 0) ($Name+' cannot reach guard, create output or discover processes')
    }else{
        Check ($errors.Count -gt 0 -and @($errors|Where-Object {$_ -notmatch 'Actual-game simulation incomplete; retain evidence at'}).Count -eq 0) ($Name+' retains intentional incomplete-run failure after full entry')
        if($Outcome -ceq 'reason_refusal' -or $Outcome -ceq 'storage_refusal'){
            Check (-not [IO.Directory]::Exists($output) -and $refusals.Count -eq 1) ($Name+' preserves refusal before output creation')
            $refusal=[IO.File]::ReadAllText($refusals[0].FullName)|ConvertFrom-Json -AsHashtable
            $expectedFailure=if($Outcome -ceq 'reason_refusal'){'MaxGeneratedBytes above 40 GiB requires an explicit GeneratedBudgetReason.'}else{'SYNTHETIC_FRESH_STORAGE_REFUSAL'}
            Check ($refusal.failures.Count -eq 1 -and $refusal.failures[0] -ceq $expectedFailure -and -not $refusal.game_started) ($Name+' retains exact refusal')
            Check ($refusal.limits.max_generated_bytes -eq $ExpectedCeiling -and $refusal.limits.generated_budget_reason -ceq $ExpectedReason) ($Name+' records refused ceiling and reason')
            Check ($state.calls.Count -eq [int]($Outcome -ceq 'storage_refusal') -and $state.process_discovery_calls -eq 0) ($Name+' performs no retry or post-admission work')
        }else{
            Check ($state.calls.Count -eq 1 -and $refusals.Count -eq 0) ($Name+' reaches storage boundary exactly once')
            $manifest=[IO.File]::ReadAllText((Join-Path $output 'run-manifest.json'))|ConvertFrom-Json -AsHashtable
            $receipt=[IO.File]::ReadAllText((Join-Path $output 'result.json'))|ConvertFrom-Json -AsHashtable
            $storage=[IO.File]::ReadAllText((Join-Path $output 'storage.json'))|ConvertFrom-Json -AsHashtable
            foreach($record in @($manifest.limits,$receipt.limits,$storage)){
                Check ($record.max_generated_bytes -eq $ExpectedCeiling -and $record.generated_budget_reason -ceq $ExpectedReason) ($Name+' records exact ceiling and reason')
            }
            $campaign=$Extra.ContainsKey('CampaignObservation') -and [bool]$Extra.CampaignObservation
            $extended=$Extra.ContainsKey('ExtendedObservation') -and [bool]$Extra.ExtendedObservation
            $mode=if($campaign){'campaign'}elseif($extended){'extended'}else{'standard'}
            foreach($record in @($manifest.limits,$receipt.limits)){
                Check ($record.campaign_observation -eq $campaign -and $record.extended_observation -eq $extended -and $record.observation_mode -ceq $mode) ($Name+' records exact observation opt-ins')
                Check ($record.cleanup_reserve_seconds -eq $(if($campaign){120}else{60}) -and $record.tape_max_frames -eq $(if($campaign){90000}else{30000})) ($Name+' records exact cleanup reserve and recorder cap')
            }
            Check ($receipt.failures.Count -eq 1 -and $receipt.failures[0] -ceq 'SYNTHETIC_STOP_AFTER_ADMISSION_MANIFEST' -and -not $receipt.actual_game_started -and $receipt.status -ceq 'failed') ($Name+' stops at synthetic dependency before game/package access')
        }
        if($state.calls.Count){
            $call=$state.calls[0];$bound=$call.bound
            $expectedKeys=@('RepositoryRoot','OutputPath','Operation','PlannedBytes','RawCaptureBytes','MaxRawCaptureBytes','MaxGeneratedBytes','AdditionalRoots')
            Check (@(Compare-Object ($bound.Keys|Sort-Object) ($expectedKeys|Sort-Object)).Count -eq 0) ($Name+' preserves exact fresh-call arguments without handoff or exclusions')
            $count=if($Extra.ContainsKey('CaptureCount')){$Extra.CaptureCount}else{4}
            $width=if($Extra.ContainsKey('CaptureWidth')){$Extra.CaptureWidth}else{1024}
            $height=if($Extra.ContainsKey('CaptureHeight')){$Extra.CaptureHeight}else{768}
            $expectedRaw=512MB+[long]$count*$width*$height*4*2*2+$count*64KB
            Check ($bound.RawCaptureBytes -eq $expectedRaw -and $bound.PlannedBytes -eq $expectedRaw+128MB -and $bound.MaxRawCaptureBytes -eq 660MB) ($Name+' preserves all eyes, streams, raw counts and planned bytes')
            Check ($call.min_free_bytes -eq 15GB -and $bound.MaxGeneratedBytes -eq $ExpectedCeiling -and $bound.Operation -ceq 'Session') ($Name+' preserves 15 GiB floor and exact requested ceiling')
            $expectedRoots=@($arguments.InstallRoot,$fixture,(Join-Path (Split-Path -Parent $fixture) 'bc2vr-task-worktrees'))
            Check ($bound.RepositoryRoot -ceq $fixture -and $bound.OutputPath -ceq $output -and ($bound.AdditionalRoots -join '|') -ceq ($expectedRoots -join '|')) ($Name+' preserves repository, output and every additional root')
        }
    }
    $cases.Add(@{name=$Name;outcome=$Outcome;storage_calls=$state.calls.Count;requested_max_generated_bytes=$ExpectedCeiling})
}

Run 'default' @{} 'admitted'
Run 'explicit-default' @{MaxGeneratedBytes=40GB} 'admitted'
$reason='Explicit 57 GiB session budget; retain complete evidence inventory and 15 GiB free floor.'
Run 'explicit-57' @{MaxGeneratedBytes=57GB;GeneratedBudgetReason=$reason} 'admitted' 57GB $reason
Run 'bounded-captures' @{MaxGeneratedBytes=57GB;GeneratedBudgetReason=$reason;CaptureCount=12;CaptureWidth=1024;CaptureHeight=768} 'admitted' 57GB $reason
Run 'extended-observation' @{ExtendedObservation=$true;Seconds=420;TimeoutSeconds=540;CaptureCount=12} 'admitted'
Run 'campaign-captures' @{CampaignObservation=$true;Seconds=1200;TimeoutSeconds=1320;CaptureCount=30;CaptureWidth=640;CaptureHeight=480} 'admitted'
Run 'campaign-oversized-captures' @{CampaignObservation=$true;Seconds=1200;TimeoutSeconds=1320;CaptureCount=30;CaptureWidth=1024;CaptureHeight=768} 'capture_refusal'
Run 'unrequested-campaign-captures' @{CaptureCount=13;CaptureWidth=640;CaptureHeight=480} 'capture_refusal'
Run 'extended-campaign-captures' @{ExtendedObservation=$true;Seconds=420;TimeoutSeconds=540;CaptureCount=13;CaptureWidth=640;CaptureHeight=480} 'capture_refusal'
Run 'small-captures' @{CaptureCount=1;CaptureWidth=64;CaptureHeight=64} 'admitted'
Run 'lower-ceiling' @{MaxGeneratedBytes=39GB} 'admitted' 39GB
Run 'missing-reason' @{MaxGeneratedBytes=57GB} 'reason_refusal' 57GB
Run 'empty-reason' @{MaxGeneratedBytes=57GB;GeneratedBudgetReason=''} 'reason_refusal' 57GB
Run 'whitespace-reason' @{MaxGeneratedBytes=57GB;GeneratedBudgetReason=" `t"} 'reason_refusal' 57GB " `t"
Run 'just-above-default' @{MaxGeneratedBytes=(40GB+1)} 'reason_refusal' (40GB+1)
Run 'storage-refused' @{MaxGeneratedBytes=57GB;GeneratedBudgetReason=$reason} 'storage_refusal' 57GB $reason $true
Run 'zero' @{MaxGeneratedBytes=0} 'invalid_size'
Run 'negative' @{MaxGeneratedBytes=-1} 'invalid_size'
Run 'nonnumeric' @{MaxGeneratedBytes='invalid'} 'invalid_size'
Run 'overflow' @{MaxGeneratedBytes='9223372036854775808'} 'invalid_size'
$result=@{passed=$true;checks=$checks.Count;details=$checks.ToArray();cases=$cases.ToArray();fixture=$fixture;evidence='deterministic full entry with synthetic imported dependencies and storage-call boundary';real_storage_scan=$false;actual_game_run=$false}
[IO.File]::WriteAllText((Join-Path $fixture 'test-result.json'),($result|ConvertTo-Json -Depth 8)+"`n",$utf8)
Write-Output "XR explicit storage budget passed: $($checks.Count) checks / $($cases.Count) cases. Synthetic boundary only; no storage scan or game. Receipt: $(Join-Path $fixture 'test-result.json')"

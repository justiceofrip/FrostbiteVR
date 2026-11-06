#requires -Version 7.5
<## Private, single-use pre-host setup child. The existing bench owns native calls. ##>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SpecificationPath,
      [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$SpecificationSha256,
      [Parameter(Mandatory)][string]$ContextPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
function Need([bool]$Ok,[string]$Why){if(-not $Ok){throw $Why}}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load([string]$Path){Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json -AsHashtable -DateKind String}
function NoLinks([string]$Path){for($at=[IO.Path]::GetFullPath($Path);$at;$at=[IO.Path]::GetDirectoryName($at)){if(Test-Path -LiteralPath $at){Need (-not((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'Linked setup path refused.'}}}
function CheckPin($Pin){Need ([IO.Path]::IsPathFullyQualified($Pin.path)) 'Absolute pinned path required.';NoLinks $Pin.path;Need ((Hash $Pin.path) -ceq $Pin.sha256 -and (Get-Item -LiteralPath $Pin.path).Length -eq $Pin.bytes) ('Setup input changed: '+$Pin.path)}
function Pin([string]$Path){@{path=[IO.Path]::GetFullPath($Path);sha256=(Hash $Path);bytes=(Get-Item -LiteralPath $Path).Length}}
function Save([string]$Name,$Data){$path=Join-Path $output $Name;Need (-not(Test-Path -LiteralPath $path)) 'Preserve setup evidence.';[IO.File]::WriteAllText($path,($Data|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))}
Need ((Hash $SpecificationPath) -ceq $SpecificationSha256) 'Pre-launch specification changed.'
$spec=Load $SpecificationPath;$context=Load $ContextPath
Need ($spec.schema -ceq 'bc2vr.pre-attach-m95-spec.v1' -and $context.schema -ceq 'bc2vr.pre-attach-context.v1') 'Wrong setup schema.'
Need ($context.specification_sha256 -ceq $SpecificationSha256 -and $spec.output_directory -ieq $context.output_directory) 'Context/specification mismatch.'
Need (($spec.maximum_seconds -is [int] -or $spec.maximum_seconds -is [long]) -and $spec.maximum_seconds -ge 15 -and $spec.maximum_seconds -le 90) 'Setup admission is bounded to 15..90 seconds.'
foreach($key in 'runner','bench','plan','python','preflight','server_capture','validator','local_config'){CheckPin $spec[$key]}
foreach($pin in $spec.dependencies){CheckPin $pin}
Need ($spec.runner.path -ieq $PSCommandPath) 'Unexpected setup runner.'
$localConfig=Load $spec.local_config.path
Need (([IO.Path]::GetFullPath((Join-Path $localConfig.game_path 'BFBC2Game.exe'))) -ieq $context.game.path) 'Read-only adapter configuration names another game.'
$plan=Load $spec.plan.path
Need ($plan.schema -ceq 'fvr.bc2.campaign_bench_plan.v1' -and $plan.planning_only -eq $true -and $plan.runtime_admission_granted -eq $false) 'Exact nonmutating bench plan required.'
$rows=@($plan.rows|Where-Object {$_.id -ceq $spec.definition})
Need ($rows.Count -eq 1 -and $spec.definition -cin $plan.loadout) 'One exact planned loadout definition required.'
$row=$rows[0]
Need ($row.name -ceq 'M95_sp' -and $row.resource -ieq 'objects/weapons/handheld/bu_sni_m95/sp_sni_m95.dbx' -and $row.resource_sha256 -ceq '8f8494c4d007bb3955485d740372bd521ff6fb4fa437f1df81602c29023ec298') 'Wrong M95 configuration.'
$variants=@($plan.provisioning.variants|Where-Object {$_.name -ceq $row.name -and ($_.path+'.dbx') -ieq $row.resource})
Need ($variants.Count -eq 1 -and $variants[0].variant -eq 0) 'Exact unscoped M95 selector 0 required.'
CheckPin $plan.provisioning.runner
$output=[IO.Path]::GetFullPath($spec.output_directory);NoLinks $output
Need (-not(Test-Path -LiteralPath $output)) 'New setup output required.'
$repository=Split-Path (Split-Path $spec.bench.path -Parent) -Parent
. (Join-Path $repository 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repository
$storage=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output -Operation Session -PlannedBytes 32MB -RawCaptureBytes 0
$null=[IO.Directory]::CreateDirectory($output);Save 'storage.json' $storage
$game=$null;$complete=$false;$failure=$null;$attempted=$false
$contextPin=Pin $ContextPath
function ExactGame {
    Need ((Hash $SpecificationPath) -ceq $SpecificationSha256 -and (Hash $ContextPath) -ceq $contextPin.sha256) 'Setup context changed.'
    $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
    try{
        Need ($games.Count -eq 1) 'Exactly one BC2 process required.'
        $candidate=$games[0]
        Need ($candidate.Id -eq $context.game.pid -and $candidate.Path -ieq $context.game.path -and
            $candidate.StartTime.ToUniversalTime().ToString('o') -ceq $context.game.start_utc -and
            [string]$candidate.StartTime.ToUniversalTime().ToFileTimeUtc() -ceq [string]$context.creation_filetime) 'Original game lifetime/path changed.'
        Need ((Hash $candidate.Path) -ceq $context.game_sha256) 'Original game executable changed.'
        Need (-not(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,ProvisionRunner -ErrorAction SilentlyContinue)) 'Setup requires idle native/VR helpers.'
        Need (@($candidate.Modules|Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count -eq 0) 'Fresh pre-host lifetime required.'
    }finally{foreach($candidate in $games){$candidate.Dispose()}}
}
function ContinueSetup {
    Need (-not(Test-Path -LiteralPath $context.stop_request) -and -not(Test-Path -LiteralPath $context.session_stop_request)) 'Setup was stopped; no further operation admitted.'
    Need ([DateTime]::UtcNow -lt [DateTimeOffset]::Parse($context.deadline_utc).UtcDateTime) 'Setup admission deadline expired.'
    foreach($key in 'runner','bench','plan','python','preflight','server_capture','validator','local_config'){CheckPin $spec[$key]}
    foreach($pin in $spec.dependencies){CheckPin $pin}
    ExactGame
}
try{
    ContinueSetup
    Save 'game.json' @{pid=$context.game.pid;creation_filetime=$context.creation_filetime;path=$context.game.path;start_utc=$context.game.start_utc}
    Save 'ready.json' @{schema='bc2vr.pre-attach-ready.v1';game=$context.game;creation_filetime=$context.creation_filetime;context=$contextPin;deadline_utc=$context.deadline_utc;request_path=(Join-Path $output 'root-request.json');no_vr_host_or_gameplay_probe=$true;normal_graphics_and_resolution_policy_retained=$true;raw_captures=0}
    $requestPath=Join-Path $output 'root-request.json'
    while(-not(Test-Path -LiteralPath $requestPath)){ContinueSetup;Start-Sleep -Milliseconds 100}
    $request=Load $requestPath
    Need ($request.schema -ceq 'bc2vr.pre-attach-m95-request.v1' -and $request.root_reviewed_campaign_on_foot -is [bool] -and $request.root_reviewed_campaign_on_foot) 'ROOT current campaign/on-foot review required.'
    Need ($request.game.pid -eq $context.game.pid -and $request.game.start_utc -ceq $context.game.start_utc -and $request.game.path -ieq $context.game.path) 'ROOT request names another lifetime.'
    Need ((Get-Item -LiteralPath $requestPath).LastWriteTimeUtc -ge [DateTimeOffset]::Parse($context.started_utc).UtcDateTime) 'Stale request refused.'
    CheckPin $request.resources
    Need ((Get-Item -LiteralPath $request.resources.path).LastWriteTimeUtc -ge [DateTimeOffset]::Parse($context.started_utc).UtcDateTime) 'Fresh resource scan required; old mount counts are insufficient.'
    Save 'accepted-request.json' @{request=(Pin $requestPath);resources=$request.resources;context=$contextPin;current_equipment_acceptance=$false}
    $common=@{Plan=$spec.plan.path;PlanSha256=$spec.plan.sha256;DefinitionId=$spec.definition;Execute=$true;GameReceipt=(Join-Path $output 'game.json');GameReceiptSha256=(Hash (Join-Path $output 'game.json'))}
    ContinueSetup;$attempted=$true
    & $spec.bench.path @common -Mode Observe -ResourceReport $request.resources.path -ResourceReportSha256 $request.resources.sha256 -Output (Join-Path $output 'observe') *> (Join-Path $output 'observe.log')
    ContinueSetup
    $prior=Join-Path $output 'observe/job.json'
    & $spec.bench.path @common -Mode Equip -PriorJob $prior -PriorJobSha256 (Hash $prior) -Output (Join-Path $output 'equip') *> (Join-Path $output 'equip.log')
    ContinueSetup
    # Existing read-only adapters; no input, refill or native resource fixtures.
    & $spec.python.path -B $spec.preflight.path --pid $context.game.pid --expected-asset M95_sp --output (Join-Path $output 'on-foot.json') *> (Join-Path $output 'on-foot.log')
    Need ($LASTEXITCODE -eq 0) 'Current on-foot/full M95 preflight failed.'
    ContinueSetup
    & $spec.python.path -B $spec.server_capture.path --pid $context.game.pid --output (Join-Path $output 'server.json') *> (Join-Path $output 'server.log')
    Need ($LASTEXITCODE -eq 0) 'Current M95 server observation failed.'
    ContinueSetup
    & $spec.python.path -B $spec.validator.path --directory $output --pid $context.game.pid *> (Join-Path $output 'validation.log')
    Need ($LASTEXITCODE -eq 0) 'Current M95 equipment join failed.'
    ContinueSetup
    $complete=$true
}catch{$failure=$_.Exception.Message}
finally{
    # A failed native operation may have retained a remote thread. Failure
    # requires ordinary exact-game exit; never claim native retirement.
    Save 'result.json' @{schema='bc2vr.pre-attach-m95-result.v1';complete=$complete;error=$failure;game=$context.game;creation_filetime=$context.creation_filetime;specification_sha256=$SpecificationSha256;context=$contextPin;native_attempted=$attempted;normal_close_required=(-not $complete);automatic_retry=$false;native_reload_accepted=$false;normal_live_owner_rig_input_revalidation_required=$true;headset_accepted=$false}
}
if(-not $complete){throw $failure}

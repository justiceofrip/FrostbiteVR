#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$FixtureRoot)
# Offline tool-contract fixtures only. All artifacts, source identities, Git answers and
# storage answers below are synthetic; the real planning, copying and admission code runs.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository=Split-Path $PSScriptRoot -Parent
. (Join-Path $repository 'tools/PrivateStage.Core.ps1')
. (Join-Path $repository 'launcher/Launcher.Core.ps1')
. (Join-Path $repository 'launcher/runtime/PrivateSession.ps1')
Assert-StageNoLinks $FixtureRoot
if(Test-Path -LiteralPath $FixtureRoot){throw 'A fresh synthetic fixture directory is required.'}
$FixtureRoot=[IO.Path]::GetFullPath($FixtureRoot)
$fixtureRepo=Join-Path $FixtureRoot 'synthetic-repository'
$batchRun=Join-Path $FixtureRoot 'synthetic-frozen-batch'
$install=Join-Path $FixtureRoot 'synthetic-install'
$fixtureLauncherCommit='b'*40;$sourceCommit='a'*40;$sourceHash='1'*64
$fixtureCalls=[Collections.Generic.List[object]]::new()
$checks=[Collections.Generic.List[string]]::new()
$storageMode=@{value='pass'}
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL: '+$Name)};$checks.Add($Name)}
function Put([string]$Path,[string]$Text){
    $null=[IO.Directory]::CreateDirectory((Split-Path $Path -Parent))
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
    return Get-StagePin $Path
}
function PutJson([string]$Path,$Value){return Put $Path ($Value|ConvertTo-Json -Depth 60)}
function Snapshot {
    return (@(Get-ChildItem -LiteralPath $FixtureRoot -Recurse -Force|Sort-Object FullName|ForEach-Object {
        if($_.PSIsContainer){'directory '+$_.FullName}else{$_.FullName+' '+$_.Length+' '+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    }) -join "`n")
}
function Refuses([scriptblock]$Action,[string]$Pattern,[string]$Name,[switch]$StorageCalled){
    $before=Snapshot;$count=@($fixtureCalls|Where-Object kind -eq storage).Count;$failure=$null
    try{$null=& $Action}catch{$failure=$_.Exception.Message}
    Check ($null -ne $failure -and $failure -match $Pattern) ($Name+': '+$failure)
    Check ((Snapshot) -ceq $before) ($Name+': no filesystem mutation')
    $after=@($fixtureCalls|Where-Object kind -eq storage).Count
    Check ($after -eq ($count+[int]$StorageCalled.IsPresent)) ($Name+': storage boundary')
}
# A scoped fake Git command avoids creating or modifying any repository.
function git {
    $fixtureCalls.Add(@{kind='git';arguments=@($args)})
    if($args[0] -cne '-C' -or $args[1] -ine $fixtureRepo){throw 'Unexpected synthetic Git repository'}
    $global:LASTEXITCODE=0
    if(($args[2..($args.Count-1)] -join ' ') -ceq 'rev-parse --verify HEAD'){return $fixtureLauncherCommit}
    if($args[2] -ceq 'status' -and $args.Count -in @(10,11)){return}
    throw ('Unexpected synthetic Git command: '+($args -join ' '))
}
$entry=Join-Path $repository 'tools/Invoke-PrivateStage.ps1'
function Plan([string]$Destination,[string]$Current='0.1.83',[string]$Fallback='0.1.77'){
    return (& $entry -Plan -BatchConfig $configPin.path -BatchConfigSha256 $configPin.sha256 -InstallRoot $install -ExpectedCurrentVersion $Current -ExpectedFallbackVersion $Fallback -StageRoot $Destination -LauncherCommit $fixtureLauncherCommit -ReviewedLauncherPins $reviewPin.path -ReviewedLauncherPinsSha256 $reviewPin.sha256 -MaxGeneratedBytes 40GB)|ConvertFrom-Json -AsHashtable -DateKind String
}
function Accept($Decision,[string]$Name){
    $Decision.root_accepted=$true;$Decision.reason='SYNTHETIC OFFLINE CONTRACT CHECK ONLY'
    return PutJson (Join-Path $FixtureRoot ($Name+'.json')) $Decision
}
function Stage($Pin){return (& $entry -Decision $Pin.path -DecisionSha256 $Pin.sha256 -RootAccept)|ConvertFrom-Json -AsHashtable -DateKind String}

$null=Put (Join-Path $FixtureRoot 'SYNTHETIC-FIXTURE.txt') 'Not a build, storage admission, installed package, native result or gameplay proof.'
foreach($relative in 'launcher/Launcher.Core.ps1','launcher/Launcher.Storage.ps1','launcher/runtime/PrivateSession.ps1'){
    $null=Put (Join-Path $fixtureRepo $relative) ([IO.File]::ReadAllText((Join-Path $repository $relative)))
}
$storagePin=Put (Join-Path $fixtureRepo 'tools/StorageBudget.ps1') @'
# SYNTHETIC STORAGE RESPONSE: no real scan, drive query, build, game or process.
function Assert-FvrStorageBudget {
    param($RepositoryRoot,$OutputPath,$Operation,$PlannedBytes,$RawCaptureBytes,$MaxGeneratedBytes,$MinFreeBytes,$AdditionalRoots)
    $fixtureCalls.Add(@{kind='storage';repository=$RepositoryRoot;output=$OutputPath;operation=$Operation;planned=$PlannedBytes;raw=$RawCaptureBytes;maximum=$MaxGeneratedBytes;floor=$MinFreeBytes;roots=@($AdditionalRoots)})
    if($storageMode.value -ceq 'refuse'){throw 'Synthetic storage refusal'}
    $roots=@($AdditionalRoots);if($storageMode.value -ceq 'missing-root'){$roots=$roots[0..11]}
    return @{schema='SYNTHETIC-OFFLINE-STORAGE';generated=@{roots=@($roots|ForEach-Object {@{path=$_}})}}
}
'@
$reviewFiles=@()
foreach($relative in 'launcher/runtime/Entry.ps1','launcher/runtime/Session.ps1','launcher/runtime/Runtime.Core.ps1','launcher/runtime/PrivateSession.ps1','tools/Invoke-Bc2XrGameSimulation.ps1'){
    $path=Join-Path $fixtureRepo $relative
    if(-not (Test-Path -LiteralPath $path)){$null=Put $path ('# SYNTHETIC inert fixture '+$relative)}
    $pin=Get-StagePin $path;$reviewFiles+=@{path=$relative;sha256=$pin.sha256;bytes=$pin.bytes}
}
$reviewPin=PutJson (Join-Path $FixtureRoot 'synthetic-reviewed-launcher.json') @{schema='fvr.private-stage-reviewed-launcher.v1';files=$reviewFiles}
$header=Put (Join-Path $fixtureRepo 'synthetic-header.txt') 'SYNTHETIC HEADER'
$sourceFile=Put (Join-Path $fixtureRepo 'src/synthetic.txt') 'SYNTHETIC SOURCE'
$recipe=Put (Join-Path $fixtureRepo 'tools/bc2_native_operation_receipts.py') '# SYNTHETIC RECIPE'
$wiring=Put (Join-Path $fixtureRepo 'cmake/Bc2NativeOperationCapability.cmake') '# SYNTHETIC WIRING'
$sourcePin=PutJson (Join-Path $FixtureRoot 'synthetic-source.json') @{schema='fvr.bc2.native-operation-source.v1';source_sha256=$sourceHash;reviewed_header_sha256=$header.sha256;recipe_sha256=$recipe.sha256;build_wiring_sha256=$wiring.sha256;files=@(@{path='src/synthetic.txt';sha256=$sourceFile.sha256});excluded_data_sha256=@()}
$roots=@(1..13|ForEach-Object {Join-Path $FixtureRoot ('synthetic-storage-root-'+$_)})
$configPin=PutJson (Join-Path $FixtureRoot 'synthetic-batch.json') @{
    schema='fvr.native-batch.v1';root_accepted=$true;repository=$fixtureRepo;run=$batchRun
    source_commit=$sourceCommit;source_sha256=$sourceHash;source_receipt=$sourcePin;host_source_receipt=$sourcePin;reviewed_header=$header
    tool_pins=@((Get-StagePin (Join-Path $fixtureRepo 'launcher/Launcher.Core.ps1')),$storagePin)
    budget=@{root_authorized=$true;freeze_ceiling_bytes=52GB;floor_bytes=15GB;roots=$roots}
}
$prerequisites=@();foreach($arch in 'x86','x64'){
    $prerequisites+=PutJson (Join-Path $batchRun ('normal-'+$arch+'/receipt.json')) @{schema='fvr.native-batch-normal.v1';normal=$true;architecture=$arch;source_sha256=$sourceHash;source_commit=$sourceCommit;config=$configPin}
}
$artifactNames=@{probe=@('x86','BC2NativeProbe.dll');trace=@('x86','BC2NativeTrace.exe');receiver=@('x64','BC2NativeIpcProbe.exe');host=@('x64','BC2XrHost.exe');resolution=@('x86','BC2NativeResolution.exe');resolution_bootstrap=@('x86','BC2ResolutionBootstrap.dll');graphics_creation32=@('x86','Bc2GraphicsCreation32.dll');graphics_creation64=@('x64','Bc2GraphicsCreation64.dll');provision_command=@('x86','Bc2CampaignProvisioningCommand.exe')}
foreach($kind in 'normal-runtime','private-x86'){
    $normal=$kind -ceq 'normal-runtime';$artifacts=@();$files=@()
    $keys=if($normal){@($artifactNames.Keys|Where-Object {$_ -cne 'provision_command'})}else{@('probe','provision_command')}
    foreach($key in $keys){
        $spec=$artifactNames[$key]
        $binary=Put (Join-Path $batchRun ($kind+'/'+$spec[1])) ('SYNTHETIC NOT EXECUTABLE '+$kind+' '+$key)
        $pdb=Put (Join-Path $batchRun ($kind+'/'+$spec[1]+'.pdb')) ('SYNTHETIC NOT SYMBOLS '+$kind+' '+$key)
        $files+=@($binary,$pdb);$artifacts+=@{key=$key;architecture=$spec[0];binary=$binary;pdb=$pdb;pe=@{guid='SYNTHETIC';age=1};symbols=@{guid='SYNTHETIC';age=1}}
    }
    $receipt=PutJson (Join-Path $batchRun ($kind+'/receipt.json')) @{schema='fvr.native-batch-artifacts.v1';source_commit=$sourceCommit;source_sha256=$sourceHash;config=$configPin;normal=$normal;private_campaign_provisioning=(-not $normal);runtime_default_enabled=$false;prerequisite_receipts=$prerequisites;artifacts=$artifacts;files=$files}
    if($normal){$normalPin=$receipt;$prerequisites=@($receipt)}
}
$null=PutJson (Join-Path $batchRun 'private-x86/shared-build-state.json') @{campaign_cache='OFF';binary_kind='private';normal_rebuild_performed=$false;normal_artifacts=$normalPin.path}
$null=Put (Join-Path $fixtureRepo 'build/x86/CMakeCache.txt') "FVR_BC2_CAMPAIGN_PROVISIONING:BOOL=OFF`n"
foreach($version in '0.1.77','0.1.83'){
    $baseline=Join-Path $install ('versions/'+$version)
    $retained=Put (Join-Path $baseline 'runtime/retained.txt') ('SYNTHETIC INSTALLED '+$version)
    $null=PutJson (Join-Path $baseline 'runtime/runtime.json') @{schema='bc2vr.runtime.v1';version=$version;diagnostic_only=$false;source_sha256=('2'*64);package_source_commit=('c'*40);files=@(@{path='retained.txt';sha256=$retained.sha256});policy='SYNTHETIC BASELINE POLICY'}
    foreach($name in 'Launcher.Core.ps1','Launcher.Storage.ps1'){$null=Put (Join-Path $baseline ('app/'+$name)) ('# SYNTHETIC installed app '+$version+' '+$name)}
}
$activePath=Join-Path $install 'active.json'
$null=PutJson $activePath @{current='0.1.83';previous='0.1.77'}
$oldStage=Put (Join-Path $batchRun 'stage/old-evidence.txt') 'PRESERVE SYNTHETIC OLD STAGE'
$oldLock=Put (Join-Path $batchRun 'private-stage-creation.lock') 'PRESERVE SYNTHETIC LEGACY LOCK'
$destination=Join-Path $batchRun 'explicit-stage83'
$before=Snapshot;$d=Plan $destination
Check ((Snapshot) -ceq $before -and @($fixtureCalls|Where-Object kind -eq storage).Count -eq 0) 'Plan is read-only and does not scan storage'
Check ($d.schema -ceq 'fvr.private-stage-decision.v2' -and -not $d.root_accepted -and $d.expected_current_version -ceq '0.1.83' -and $d.expected_fallback_version -ceq '0.1.77' -and $d.stage_root -ieq $destination) 'CLI propagates explicit 83/77 identities and fresh destination'
$plan=Get-PrivateStageCopyPlan $d
Check ($plan.files.Count -eq 16 -and (Test-StageSameValue $plan.estimate $d.estimate)) 'copy plan includes retained file, nine frozen binaries, four reviewed scripts and two apps'
Check (@($plan.files|Where-Object {$_.origin -like 'retained-installed*' -and $_.source.path -notlike '*0.1.83*'}).Count -eq 0) 'copy sources select only explicitly expected baseline83'
Refuses {Plan (Join-Path $batchRun 'stale-version') '0.1.77' '0.1.76'} 'current/fallback identity differs' 'stale expected installation'
Refuses {Plan (Join-Path $batchRun 'wrong-fallback') '0.1.83' '0.1.76'} 'current/fallback identity differs' 'mismatched fallback'
Refuses {Plan (Join-Path $FixtureRoot 'outside-batch')} 'inside the exact batch run' 'out-of-batch destination'
Refuses {Plan (Join-Path $batchRun 'missing-parent/stage')} 'parent must already exist' 'missing parent'
Refuses {Plan (Join-Path $batchRun 'unsafe.')} 'unsafe stage destination spelling' 'unsafe destination spelling'
if($IsWindows){
    $linkTarget=Join-Path $FixtureRoot 'synthetic-link-target';$null=[IO.Directory]::CreateDirectory($linkTarget)
    $junction=Join-Path $batchRun 'synthetic-junction';$null=New-Item -ItemType Junction -Path $junction -Target $linkTarget
    try{Refuses {Plan (Join-Path $junction 'stage')} 'linked path' 'reparse ancestor'}
    finally{
        # Retain the fixture data, but never leave a synthetic link in the counted
        # workspace: the next real complete storage inventory must reject it.
        $link=Get-Item -LiteralPath $junction -Force
        if($link.LinkType -cne 'Junction' -or [IO.Path]::GetFullPath([string]$link.Target) -ine [IO.Path]::GetFullPath($linkTarget)){throw 'Synthetic junction identity changed'}
        [IO.Directory]::Delete($junction)
    }
    Check (-not (Test-Path -LiteralPath $junction) -and (Test-Path -LiteralPath $linkTarget -PathType Container)) 'synthetic link removed while target data remains'
}
Refuses {Plan (Join-Path $batchRun 'stage')} 'stage already exists' 'occupied old destination'
$occupied=Join-Path $batchRun 'occupied-lock'
$occupiedPin=Put ($occupied+'.private-stage-creation.lock') 'PRESERVE SYNTHETIC ATTEMPT'
Refuses {Plan $occupied} 'prior staging attempt exists' 'occupied destination lock'
$manifestPath=Join-Path $install 'versions/0.1.83/runtime/runtime.json';$manifestText=[IO.File]::ReadAllText($manifestPath)
$wrong=Read-StageJson $manifestPath;$wrong.version='0.1.82';$null=PutJson $manifestPath $wrong
Refuses {Plan (Join-Path $batchRun 'wrong-manifest')} 'expected installed normal manifest' 'mismatched baseline manifest'
$null=Put $manifestPath $manifestText
$decisionPin=Accept $d 'synthetic-accepted-decision'
$null=PutJson $activePath @{current='0.1.84';previous='0.1.83'}
Refuses {Stage $decisionPin} 'current/fallback identity differs' 'new installation after Plan is not auto-selected'
$null=PutJson $activePath @{current='0.1.83';previous='0.1.77';changed='SYNTHETIC MUTATION'}
Refuses {Stage $decisionPin} 'reviewed decision changed: installed_active' 'active file changed after Plan'
$null=PutJson $activePath @{current='0.1.83';previous='0.1.77'}
$retainedPath=Join-Path $install 'versions/0.1.83/runtime/retained.txt';$retainedText=[IO.File]::ReadAllText($retainedPath)
$null=Put $retainedPath 'SYNTHETIC CHANGED INSTALLED FILE'
Refuses {Stage $decisionPin} 'installed file changed' 'installed payload changed after Plan'
$null=Put $retainedPath $retainedText
$historical=Read-StageJson $decisionPin.path;$historical.schema='fvr.private-stage-decision.v1'
$historicalPin=PutJson (Join-Path $FixtureRoot 'synthetic-historical-decision.json') $historical
Refuses {Stage $historicalPin} 'historical decisions must not be reused' 'v1 decision cannot become fresh'
$draft=Plan (Join-Path $batchRun 'unaccepted-stage');$draftPin=PutJson (Join-Path $FixtureRoot 'synthetic-draft.json') $draft
Refuses {Stage $draftPin} 'ROOT-accepted decision' 'unaccepted decision'
$changed=Read-StageJson $decisionPin.path;$changed.expected_fallback_version='0.1.76'
$changedPin=PutJson (Join-Path $FixtureRoot 'synthetic-mismatched-decision.json') $changed
Refuses {Stage $changedPin} 'current/fallback identity differs' 'Stage propagates pinned expected fallback'
$changed=Read-StageJson $decisionPin.path;$changed.stage_root=Join-Path $FixtureRoot 'outside-stage'
$changedPin=PutJson (Join-Path $FixtureRoot 'synthetic-outside-decision.json') $changed
Refuses {Stage $changedPin} 'inside the exact batch run' 'Stage propagates pinned destination'
$storageMode.value='refuse'
Refuses {Stage $decisionPin} 'Synthetic storage refusal' 'storage refusal before lock/copy' -StorageCalled
$storageMode.value='missing-root'
Refuses {Stage $decisionPin} 'exact accepted 13 roots' 'incomplete storage inventory before lock/copy' -StorageCalled
$storageMode.value='pass'
$raceDestination=Join-Path $batchRun 'occupied-after-plan';$racePin=Accept (Plan $raceDestination) 'synthetic-race-decision'
$null=Put (Join-Path $raceDestination 'evidence.txt') 'PRESERVE SYNTHETIC OCCUPANT'
Refuses {Stage $racePin} 'stage already exists' 'destination appeared after Plan'
$raceDestination=Join-Path $batchRun 'locked-after-plan';$racePin=Accept (Plan $raceDestination) 'synthetic-lock-race-decision'
$null=Put ($raceDestination+'.private-stage-creation.lock') 'PRESERVE SYNTHETIC LOCK'
Refuses {Stage $racePin} 'prior staging attempt exists' 'lock appeared after Plan'
$result=Stage $decisionPin
Check ($result.passed -and -not $result.game_launched -and -not $result.native_build_started) 'assembled CLI Stage passes real final admission with synthetic files'
$storedPlan=Read-StageJson (Join-Path $destination 'copy-plan.json')
Check (Test-StageSameValue $plan $storedPlan) 'Stage preserves reviewed copy plan'
$binding=Read-StageJson $result.binding.path
Check ($binding.baseline_version -ceq '0.1.83' -and $binding.installed_active.sha256 -ceq $d.installed_active.sha256 -and $binding.runtime_root -ieq (Join-Path $destination 'runtime')) 'binding uses explicit baseline, pinned active identity and stage destination'
$verified=Get-Bc2PrivateAdmission $result.binding.path $result.binding.sha256 $binding.runtime_root $install
Check ($verified.baseline.version -ceq '0.1.83') 'unchanged real admission guard accepts parameterized baseline'
$budget=@($fixtureCalls|Where-Object kind -eq storage)[-1]
Check ($budget.repository -ieq $fixtureRepo -and $budget.output -ieq $destination -and $budget.operation -ceq 'Build' -and $budget.planned -eq $plan.estimate.planned_bytes -and $budget.raw -eq 0 -and $budget.maximum -eq 40GB -and $budget.floor -eq 15GB -and (Test-StageSameValue $budget.roots $roots)) 'Stage propagates estimate, ceiling, 15GiB floor and all 13 roots'
Check ((Test-Path -LiteralPath ($destination+'.private-stage-creation.lock')) -and (Test-Path -LiteralPath (Join-Path $destination 'result.json'))) 'successful destination retains exclusive attempt lock'
foreach($pin in @($oldStage,$oldLock,$occupiedPin,$configPin)){Assert-StagePin $pin}
Check $true 'old stage, legacy lock, occupied lock and immutable batch receipt preserved'
Refuses {Stage $decisionPin} 'stage already exists' 'accepted decision cannot be reused after success'
$successor=Plan (Join-Path $batchRun 'another-explicit-stage83')
Check ($successor.native_batch_config.sha256 -ceq $configPin.sha256 -and $successor.stage_root -ine $d.stage_root) 'same frozen receipt supports another explicit fresh plan'
[pscustomobject]@{passed=$true;checks=$checks.Count;fixture_root=$FixtureRoot;evidence_level='offline synthetic tool-contract checks only';real_storage_scans=0;gameplay_proof=$false;native_builds=0}|ConvertTo-Json

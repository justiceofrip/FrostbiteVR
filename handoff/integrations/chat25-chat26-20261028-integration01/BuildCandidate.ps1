[CmdletBinding()]
param([ValidateSet('Prepare','x86','x64')][string]$Mode='Prepare',[ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Attempt='01',
      [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$run=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01'
$priorRoot=Join-Path $repo 'reports/integration-chat04-20261028-01'
$python='G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'
$lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw | ConvertFrom-Json
if($lease.owner_token -ne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'Integration lease not owned'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost,BC2NativeResolution -ErrorAction SilentlyContinue){throw 'Shared build/session resources not idle'}
function Sha([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$value){
    if(Test-Path -LiteralPath $p){throw "Preserve existing evidence: $p"}
    [IO.File]::WriteAllText($p,($value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
}
. (Join-Path $repo 'tools/StorageBudget.ps1')
# Preparation verifies existing fallback copies and writes small provenance files;
# it never copies runtime files or compiles. Each build reserves the full 3GiB.
$planned=if($Mode -eq 'Prepare'){16MB}else{3GB}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes $planned -MaxGeneratedBytes $MaxGeneratedBytes -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees')
[void][IO.Directory]::CreateDirectory($run)
Save (Join-Path $run "$Mode-$Attempt-storage.json") $admission
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$helper=Join-Path $repo 'tools/bc2_native_operation_receipts.py'
$proofDir=Join-Path $run 'provenance'
$head=((& git -C $repo rev-parse HEAD)-join '')
if($LASTEXITCODE -or $head -notmatch '^[0-9a-f]{40}$'){throw 'Cannot pin candidate commit'}
$taskCommit='d55b3c6249c9f56e4235f7805a80d334a5dc610f'
$mergeCommit='636d7a4b149bc07f43dd13a53a44a6a9862bd563'
& git -C $repo merge-base --is-ancestor $mergeCommit $head
if($LASTEXITCODE){throw 'Candidate lacks the requested integration merge'}
& git -C $repo merge-base --is-ancestor $taskCommit $head
if($LASTEXITCODE){throw 'Candidate lacks the requested exact task commit'}
if($Mode -eq 'Prepare'){
    if(Test-Path -LiteralPath $proofDir){throw 'Preserve existing preparation'}
    $priorReceipt=Join-Path $priorRoot 'normal-runtime/build-receipt.json'
    if((Sha $priorReceipt) -ne 'f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950'){throw 'Prior runtime receipt changed'}
    $prior=Get-Content -LiteralPath $priorReceipt -Raw | ConvertFrom-Json
    if((Sha $prior.source_freeze.path) -ne $prior.source_freeze.sha256){throw 'Prior source freeze changed'}
    $freeze=Get-Content -LiteralPath $prior.source_freeze.path -Raw | ConvertFrom-Json -AsHashtable
    if((Sha $freeze.reviewed_header.path) -ne $freeze.reviewed_header.sha256 -or
       (Sha $freeze.source_receipt.path) -ne $freeze.source_receipt.sha256){throw 'Reviewed native operation evidence changed'}
    $fallback=@(foreach($key in @('probe','trace','receiver','host','resolution','resolution_bootstrap')){
        foreach($kind in @('binaries','symbols_by_binary')){
            $value=$prior.$kind.$key
            $expectedRoot=[IO.Path]::GetFullPath((Join-Path $priorRoot 'normal-runtime'))
            if(-not $value.path -or [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($value.path)) -ne $expectedRoot){throw 'Fallback is not an existing CHAT04 frozen copy'}
            if((Sha $value.path) -ne $value.sha256 -or (Get-Item -LiteralPath $value.path).Length -ne $value.bytes){throw "Fallback pin changed: $($value.path)"}
            @{key=$key;kind=$kind;path=$value.path;bytes=$value.bytes;sha256=$value.sha256}
        }
    })
    if($fallback.Count -ne 12){throw 'Expected all six frozen binary/PDB pairs'}
    Save (Join-Path $run 'fallback-verified.json') @{prior_receipt=$priorReceipt;prior_receipt_sha256=(Sha $priorReceipt);copies_created=0;files=$fallback}
    [void][IO.Directory]::CreateDirectory($proofDir)
    & $python -B $helper source --root $repo --output (Join-Path $proofDir 'source-without-header.json')
    if($LASTEXITCODE){throw 'Source closure failed'}
    $closure=Get-Content -LiteralPath (Join-Path $proofDir 'source-without-header.json') -Raw | ConvertFrom-Json
    $text=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($freeze.reviewed_header.path))
    if([regex]::Matches($text,[regex]::Escape($prior.source_sha256)).Count -ne 1){throw 'Expected exactly one source binding'}
    $rebound=$text.Replace($prior.source_sha256,$closure.source_sha256)
    if($rebound.Replace($closure.source_sha256,$prior.source_sha256) -cne $text){throw 'Operation records changed'}
    $header=Join-Path $proofDir 'Bc2NativeOperationCapabilities.generated.h'
    [IO.File]::WriteAllBytes($header,[Text.Encoding]::Latin1.GetBytes($rebound))
    & $python -B $helper source --root $repo --reviewed-header $header --output (Join-Path $proofDir 'source-with-header.json')
    if($LASTEXITCODE){throw 'Source/header receipt failed'}
    $flags=@{}
    foreach($arch in @('x86','x64')){
        $cache=Get-Content -LiteralPath (Join-Path $repo "build/$arch/CMakeCache.txt")
        foreach($entry in $freeze.normal_flags[$arch].GetEnumerator()){
            $expected=$entry.Key+':'+$entry.Value.type+'='+$entry.Value.value
            if($cache -cnotcontains $expected){throw "Normal flag mismatch: $arch $expected"}
        }
        $flags[$arch]=$freeze.normal_flags[$arch]
    }
    Save (Join-Path $run 'source-state.json') @{source_commit=$head;task_commit=$taskCommit;merge_commit=$mergeCommit;source_sha256=$closure.source_sha256;reviewed_header=$header;source_receipt=(Join-Path $proofDir 'source-with-header.json');normal_flags=$flags;operation_records_changed=$false;native_acceptance=$false;prior_receipt=$priorReceipt;prior_receipt_sha256=(Sha $priorReceipt)}
    Write-Output ('Prepared source '+$closure.source_sha256+'; all twelve existing CHAT04 fallback files verified without copying.');return
}
$state=Get-Content -LiteralPath (Join-Path $run 'source-state.json') -Raw | ConvertFrom-Json -AsHashtable
& $python -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256
if($LASTEXITCODE){throw 'Frozen candidate source verification failed'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$arch=if($Mode -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$build=Join-Path $repo "build/$Mode"
& cmake -S $repo -B $build "-DBC2_NATIVE_OPERATION_CAPABILITIES_HEADER:FILEPATH=$($state.reviewed_header.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_RECEIPT:FILEPATH=$($state.source_receipt.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_SHA256:STRING=$($state.source_sha256)" *> (Join-Path $run "$Mode-$Attempt-configure-binding.log")
if($LASTEXITCODE){Get-Content -LiteralPath (Join-Path $run "$Mode-$Attempt-configure-binding.log") -Tail 40;throw 'Candidate binding configure failed'}
$cache=Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
foreach($entry in $state.normal_flags[$Mode].GetEnumerator()){
    if($entry.Key -in @('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER','BC2_NATIVE_OPERATION_SOURCE_RECEIPT','BC2_NATIVE_OPERATION_SOURCE_SHA256')){continue}
    if($cache -cnotcontains ($entry.Key+':'+$entry.Value.type+'='+$entry.Value.value)){throw "Normal flag changed: $($entry.Key)"}
}
$failure=$null
$buildStarted=[DateTime]::UtcNow
$builtCommit=$head
$focusedPassed=$false
$normalStarted=$null
try {
    & cmake --build $build --target MeleeStrokeTests SelectedMeleeAttackTests KnifeMotionBoundaryTests SceneRecoveryProbeTests --parallel 4 *> (Join-Path $run "$Mode-$Attempt-focused-build.log")
    if($LASTEXITCODE){throw "Focused compile failed ($LASTEXITCODE)"}
    & ctest --test-dir $build --output-on-failure -R '^(MeleeStroke|SelectedMeleeAttack|KnifeMotionBoundary|SceneRecoveryProbe)$' *> (Join-Path $run "$Mode-$Attempt-focused-tests.log")
    if($LASTEXITCODE){throw "Focused tests failed ($LASTEXITCODE)"}
    $focusedPassed=$true
    Save (Join-Path $run "$Mode-$Attempt-focused-result.json") @{source_commit=$builtCommit;source_sha256=$state.source_sha256;architecture=$Mode;passed=$true;suite_count=4;completed_utc=[DateTime]::UtcNow.ToString('o')}
    $normalStarted=[DateTime]::UtcNow
    & (Join-Path $repo 'Build.ps1') -Architecture $Mode -Jobs 4 -MaxGeneratedBytes $MaxGeneratedBytes *> (Join-Path $run "$Mode-$Attempt-build-console.log")
} catch { $failure=$_ }
if($null -ne $normalStarted){
    foreach($name in @('configure.log','build.log','tests.log','build.json')){
        $source=Join-Path $repo "reports/$Mode-$name"
        if(-not (Test-Path -LiteralPath $source)){continue}
        if((Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $normalStarted){continue}
        if($name -eq 'build.json' -and $failure){continue}
        Copy-Item -LiteralPath $source -Destination (Join-Path $run "$Mode-$Attempt-$name")
    }
}
Save (Join-Path $run "$Mode-$Attempt-result.json") @{source_commit=$builtCommit;source_sha256=$state.source_sha256;started_utc=$buildStarted.ToString('o');completed_utc=[DateTime]::UtcNow.ToString('o');focused_passed=$focusedPassed;passed=($null -eq $failure);failure=if($failure){$failure.ToString()}else{$null}}
foreach($name in @('focused-build.log','focused-tests.log','build-console.log')){
    $path=Join-Path $run "$Mode-$Attempt-$name"
    if(Test-Path -LiteralPath $path){Get-Content -LiteralPath $path -Tail 10}
}
if($failure){throw $failure}
$lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw | ConvertFrom-Json
if($lease.owner_token -ne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'Integration lease changed'}
& $python -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256
if($LASTEXITCODE){throw 'Candidate source changed during build'}
Write-Output "$Mode focused checks, normal build and all tests completed."

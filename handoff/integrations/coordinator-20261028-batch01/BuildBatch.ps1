[CmdletBinding()]
param([ValidateSet('Prepare','x86','x64')][string]$Mode='Prepare',[string]$Attempt='01')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$run=Join-Path $repo 'reports/coordinator-20261028-batch01'
$token='campaign-coordinator-20261028-r1-batch01-70571f7dcb734b658814f1d2a17765c1'
$leasePath='G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
if((Get-Content -LiteralPath $leasePath -Raw|ConvertFrom-Json).owner_token -ne $token){throw 'Integration lease not owned'}
if($Attempt -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid attempt'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost,BC2NativeResolution -ErrorAction SilentlyContinue){throw 'Shared resources not idle'}
function Save($path,$value){if(Test-Path -LiteralPath $path){throw "Preserve existing evidence: $path"};[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))}
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$planned=if($Mode -eq 'Prepare'){16MB}else{3GB}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes $planned -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees')
[void][IO.Directory]::CreateDirectory($run)
Save (Join-Path $run "$Mode-$Attempt-storage.json") $admission
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$python='G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'
$helper=Join-Path $repo 'tools/bc2_native_operation_receipts.py'
$head=((& git -C $repo rev-parse HEAD)-join '')
if($LASTEXITCODE -or $head -notmatch '^[0-9a-f]{40}$'){throw 'Cannot pin source'}
foreach($required in @('4448779e33ad265f572245cff49913983696ce5c','b19b770')){& git -C $repo merge-base --is-ancestor $required $head;if($LASTEXITCODE){throw 'Missing accepted source or task'}}
$statePath=Join-Path $run 'source-state.json'
if($Mode -eq 'Prepare'){
    $priorPath=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01/normal-runtime/build-receipt.json'
    if((Sha $priorPath) -ne 'dd0c8b845b72c1999f337b97b680692d3c74253ec8bb3237dc768f8074509095'){throw 'Prior receipt changed'}
    $prior=Get-Content -LiteralPath $priorPath -Raw|ConvertFrom-Json
    if((Sha $prior.source_freeze.path) -ne $prior.source_freeze.sha256){throw 'Prior freeze changed'}
    $freeze=Get-Content -LiteralPath $prior.source_freeze.path -Raw|ConvertFrom-Json -AsHashtable
    foreach($pin in @($freeze.reviewed_header,$freeze.source_receipt)){if((Sha $pin.path) -ne $pin.sha256){throw 'Prior provenance changed'}}
    $files=@(foreach($key in @('probe','trace','receiver','host','resolution','resolution_bootstrap')){foreach($kind in @('binaries','symbols_by_binary')){
        $pin=$prior.$kind.$key
        if((Sha $pin.path) -ne $pin.sha256 -or (Get-Item -LiteralPath $pin.path).Length -ne $pin.bytes){throw 'Prior artifact changed'}
        $pin
    }})
    if($files.Count -ne 12){throw 'Expected six binary/PDB pairs'}
    Save (Join-Path $run 'fallback-verified.json') @{prior_receipt=$priorPath;prior_receipt_sha256=(Sha $priorPath);copies_created=0;files=$files}
    $proof=Join-Path $run 'provenance';if(Test-Path -LiteralPath $proof){throw 'Preserve preparation'};[void][IO.Directory]::CreateDirectory($proof)
    & $python -B $helper source --root $repo --output (Join-Path $proof 'source-without-header.json');if($LASTEXITCODE){throw 'Source closure failed'}
    $closure=Get-Content -LiteralPath (Join-Path $proof 'source-without-header.json') -Raw|ConvertFrom-Json
    $old=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($freeze.reviewed_header.path))
    if([regex]::Matches($old,[regex]::Escape($prior.source_sha256)).Count -ne 1){throw 'Expected one source binding'}
    $new=$old.Replace($prior.source_sha256,$closure.source_sha256)
    if($new.Replace($closure.source_sha256,$prior.source_sha256) -cne $old){throw 'Operation records changed'}
    $header=Join-Path $proof 'Bc2NativeOperationCapabilities.generated.h';[IO.File]::WriteAllBytes($header,[Text.Encoding]::Latin1.GetBytes($new))
    $source=Join-Path $proof 'source-with-header.json'
    & $python -B $helper source --root $repo --reviewed-header $header --output $source;if($LASTEXITCODE){throw 'Header receipt failed'}
    foreach($arch in @('x86','x64')){$cache=Get-Content -LiteralPath (Join-Path $repo "build/$arch/CMakeCache.txt");foreach($entry in $freeze.normal_flags[$arch].GetEnumerator()){
        if($cache -cnotcontains ($entry.Key+':'+$entry.Value.type+'='+$entry.Value.value)){throw "Normal flag changed: $arch $($entry.Key)"}
    }}
    Save $statePath @{source_commit=$head;task_commit=((& git -C $repo rev-parse b19b770)-join '');merge_commit='a95d868f36d2bc8fce40b0f17e519b639e517aa8';source_sha256=$closure.source_sha256;reviewed_header=$header;source_receipt=$source;normal_flags=$freeze.normal_flags;prior_receipt=$priorPath;prior_receipt_sha256=(Sha $priorPath);operation_records_changed=$false;native_acceptance=$false}
    Write-Output "Prepared $head with unchanged operation rows and twelve verified prior artifacts";return
}
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json -AsHashtable
& $python -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256;if($LASTEXITCODE){throw 'Source changed'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$arch=if($Mode -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation|Out-Null
$build=Join-Path $repo "build/$Mode"
$failure=$null;$focused=$false;$normalStarted=$null;$started=[DateTime]::UtcNow
try {
& cmake -S $repo -B $build "-DBC2_NATIVE_OPERATION_CAPABILITIES_HEADER:FILEPATH=$($state.reviewed_header.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_RECEIPT:FILEPATH=$($state.source_receipt.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_SHA256:STRING=$($state.source_sha256)" *> (Join-Path $run "$Mode-$Attempt-configure-binding.log")
if($LASTEXITCODE){throw 'Binding configure failed'}
$cache=Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
foreach($entry in $state.normal_flags[$Mode].GetEnumerator()){
    if($entry.Key -like 'BC2_NATIVE_OPERATION_*'){continue}
    if($cache -cnotcontains ($entry.Key+':'+$entry.Value.type+'='+$entry.Value.value)){throw "Normal flag changed: $($entry.Key)"}
}
    & cmake --build $build --target Bc2PlayerGrenadeTests Bc2NormalPlayerToolsConfigTests Bc2BodyAmmoTests Bc2BodyPropPairTests --parallel 4 *> (Join-Path $run "$Mode-$Attempt-focused-build.log")
    if($LASTEXITCODE){throw 'Focused compile failed'}
    & ctest --test-dir $build --output-on-failure -R '^(Bc2PlayerGrenade|Bc2NormalPlayerToolsConfig|Bc2BodyAmmo|Bc2BodyPropPair)$' *> (Join-Path $run "$Mode-$Attempt-focused-tests.log")
    if($LASTEXITCODE){throw 'Focused tests failed'}
    $focused=$true;Save (Join-Path $run "$Mode-$Attempt-focused-result.json") @{source_commit=$head;source_sha256=$state.source_sha256;architecture=$Mode;passed=$true;suite_count=4;completed_utc=[DateTime]::UtcNow.ToString('o')}
    $normalStarted=[DateTime]::UtcNow
    & (Join-Path $repo 'Build.ps1') -Architecture $Mode -Jobs 4 *> (Join-Path $run "$Mode-$Attempt-build-console.log")
    if((Get-Content -LiteralPath $leasePath -Raw|ConvertFrom-Json).owner_token -ne $token){throw 'Lease changed'}
    & $python -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256
    if($LASTEXITCODE){throw 'Source changed during build'}
} catch {$failure=$_}
if($normalStarted){foreach($name in @('configure.log','build.log','tests.log','build.json')){
    $path=Join-Path $repo "reports/$Mode-$name"
    if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $normalStarted -and -not($name -eq 'build.json' -and $failure)){Copy-Item -LiteralPath $path -Destination (Join-Path $run "$Mode-$Attempt-$name")}
}}
Save (Join-Path $run "$Mode-$Attempt-result.json") @{source_commit=$head;source_sha256=$state.source_sha256;started_utc=$started.ToString('o');completed_utc=[DateTime]::UtcNow.ToString('o');focused_passed=$focused;passed=($null -eq $failure);failure=if($failure){$failure.ToString()}else{$null}}
foreach($name in @('focused-build.log','focused-tests.log','build-console.log')){$path=Join-Path $run "$Mode-$Attempt-$name";if(Test-Path -LiteralPath $path){Get-Content -LiteralPath $path -Tail 12}}
if($failure){throw $failure}

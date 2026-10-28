[CmdletBinding()]
param([ValidateSet('Prepare','x86','x64')][string]$Mode='Prepare',[string]$Attempt='01',
      [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$run=Join-Path $repo 'reports/integration-chat04-20261028-01'
$priorRoot='G:\Unity\VRMMO\bc2vr-recovery\performance436\root-custody-normal-r56-prep\build01'
$python='G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'
$lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw | ConvertFrom-Json
if($lease.owner_token -ne 'f6b94650-377a-4591-a144-176cbed9f353'){throw 'Integration lease not owned'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost,BC2NativeResolution -ErrorAction SilentlyContinue){throw 'Shared build/session resources not idle'}
function Sha([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$value){
    if(Test-Path -LiteralPath $p){throw "Preserve existing evidence: $p"}
    [IO.File]::WriteAllText($p,($value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
}
. (Join-Path $repo 'tools/StorageBudget.ps1')
# Preparation only preserves five symbols and writes small provenance files;
# it never configures or compiles. Each actual normal build still reserves 3GiB.
$planned=if($Mode -eq 'Prepare'){512MB}else{3GB}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes $planned -MaxGeneratedBytes $MaxGeneratedBytes -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees\chat04-20261028-tracking-custody-r1\test-temp')
[void][IO.Directory]::CreateDirectory($run)
Save (Join-Path $run "$Mode-$Attempt-storage.json") $admission
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$helper=Join-Path $repo 'tools/bc2_native_operation_receipts.py'
$proofDir=Join-Path $run 'provenance'
if($Mode -eq 'Prepare'){
    if(Test-Path -LiteralPath $proofDir){throw 'Preserve existing preparation'}
    $priorReceipt=Join-Path $priorRoot 'normal-runtime/build-receipt.json'
    if((Sha $priorReceipt) -ne '1bbce04a887c1b9c022d0d4aeb6f58e5df0dd1faa28b4c23fb79db8f2e7db002'){throw 'Prior runtime receipt changed'}
    $prior=Get-Content -LiteralPath $priorReceipt -Raw | ConvertFrom-Json
    if((Sha $prior.source_freeze.path) -ne $prior.source_freeze.sha256){throw 'Prior source freeze changed'}
    $freeze=Get-Content -LiteralPath $prior.source_freeze.path -Raw | ConvertFrom-Json -AsHashtable
    if((Sha $freeze.reviewed_header.path) -ne $freeze.reviewed_header.sha256 -or
       (Sha $freeze.source_receipt.path) -ne $freeze.source_receipt.sha256){throw 'Reviewed native operation evidence changed'}
    $fallback=Join-Path $run 'r56-fallback-symbols';[void][IO.Directory]::CreateDirectory($fallback)
    $audit=Get-Content -LiteralPath (Join-Path $priorRoot 'independent-runtime-audit01.json') -Raw | ConvertFrom-Json
    $pins=@{
        'x86/symbols/BC2NativeTrace.pdb'='b2c2b7473f45f9433430931a97c1c78a66bb3beb12d79c5f18a0494d61cce385'
        'x86/symbols/BC2ResolutionBootstrap.pdb'='d26082bd5121701bb61f77e8fa2e56b957d053861be7ee4230e9d3ca863f69de'
        'x64/symbols/BC2NativeIpcProbe.pdb'='2a25cb0ff245811dc24be0725834c298b2fdcd68ccfbea5fb00e554297030741'
        'x64/symbols/BC2XrHost.pdb'='9a89aa5bac60a854fb06b3fea7e750b17d950df8704e8500ba5d96c1fd3aed7f'
        'x86/symbols/BC2NativeResolution.pdb'='60e4886a06ff04bdbea3fb0754f1dcffb0a14514aa2356194d9aaadf032b125b'
    }
    $saved=@(foreach($name in $pins.Keys){
        $source=Join-Path $repo ('build/'+$name);if((Sha $source) -ne $pins[$name]){throw "Fallback symbol changed: $source"}
        $target=Join-Path $fallback ([IO.Path]::GetFileName($name));Copy-Item -LiteralPath $source -Destination $target
        if((Sha $target) -ne $pins[$name]){throw 'Fallback symbol copy mismatch'}
        @{source=$source;path=$target;sha256=$pins[$name]}
    })
    Save (Join-Path $run 'fallback-symbols.json') $saved
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
    Save (Join-Path $run 'source-state.json') @{source_commit=((& git -C $repo rev-parse HEAD)-join '');source_sha256=$closure.source_sha256;reviewed_header=$header;source_receipt=(Join-Path $proofDir 'source-with-header.json');normal_flags=$flags;operation_records_changed=$false;native_acceptance=$false;prior_receipt=$priorReceipt;prior_receipt_sha256=(Sha $priorReceipt)}
    Write-Output ('Prepared source '+$closure.source_sha256+'; five prior runtime symbol files preserved.');return
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
try { & (Join-Path $repo 'Build.ps1') -Architecture $Mode -Jobs 4 -MaxGeneratedBytes $MaxGeneratedBytes *> (Join-Path $run "$Mode-$Attempt-build-console.log") } catch { $failure=$_ }
foreach($name in @('configure.log','build.log','tests.log','build.json')){
    $source=Join-Path $repo "reports/$Mode-$name"
    if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $run "$Mode-$Attempt-$name")}
}
Get-Content -LiteralPath (Join-Path $run "$Mode-$Attempt-build-console.log") -Tail 20
if($failure){throw $failure}
& $python -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256
if($LASTEXITCODE){throw 'Candidate source changed during build'}
Write-Output "$Mode normal build and tests completed."

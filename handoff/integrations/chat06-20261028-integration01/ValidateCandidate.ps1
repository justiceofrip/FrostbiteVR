[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('x86','x64')][string]$Architecture)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$run=Join-Path $repo 'reports/integration-chat06-20261028-01'
$lease=Get-Content -LiteralPath 'G:/Unity/VRMMO/bc2vr-task-worktrees/INTEGRATION.lock' -Raw | ConvertFrom-Json
if($lease.owner_token -cne 'chat06-20261028-integration01-d8f34653-e401-4e28-ab97-6d04aebf5f47'){throw 'Integration lease not owned'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost -ErrorAction SilentlyContinue){throw 'Shared resources are not idle'}
function Pin([string]$Path){$file=Get-Item -LiteralPath $Path;return @{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Save([string]$Path,$Value){if(Test-Path -LiteralPath $Path){throw "Preserve existing evidence: $Path"};[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
$priorPath=Join-Path $repo 'reports/integration-chat04-20261028-01/normal-runtime/build-receipt.json'
if((Pin $priorPath).sha256 -cne 'f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950'){throw 'Frozen runtime receipt changed'}
$prior=Get-Content -LiteralPath $priorPath -Raw | ConvertFrom-Json -AsHashtable
$freeze=Get-Content -LiteralPath $prior.source_freeze.path -Raw | ConvertFrom-Json -AsHashtable
$build=Join-Path $repo "build/$Architecture"
$cache=Join-Path $build 'CMakeCache.txt'
$cacheBefore=Pin $cache
$cacheText=Get-Content -LiteralPath $cache
if($cacheText -cnotcontains ('CMAKE_HOME_DIRECTORY:INTERNAL='+$repo.Replace('\','/'))){throw 'Canonical source binding differs'}
foreach($entry in $freeze.normal_flags[$Architecture].GetEnumerator()){
    if($cacheText -cnotcontains ($entry.Key+':'+$entry.Value.type+'='+$entry.Value.value)){throw "Normal flag changed: $($entry.Key)"}
}
. (Join-Path $repo 'tools/StorageBudget.ps1')
# Only two CPU test targets are rebuilt; no runtime build/configure or payload.
# This bounded target estimate is separate from Build.ps1's full 3GiB reservation.
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes 256MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees')
$null=[IO.Directory]::CreateDirectory($run)
Save (Join-Path $run "$Architecture-storage.json") $admission
$python='G:/Unity/VRMMO/bc2vr-recovery/overnight426/release/stage-0.1.8-01/runtime/python/python.exe'
$helper=Join-Path $repo 'tools/bc2_native_operation_receipts.py'
& $python -B $helper verify-source --root $repo --reviewed-header $freeze.reviewed_header.path --receipt $freeze.source_receipt.path --expected $prior.source_sha256 *> (Join-Path $run "$Architecture-source-guard.log")
if($LASTEXITCODE){throw 'Unchanged runtime source closure check failed'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$targets=@('Bc2NativeCycleServiceTests','Bc2AmmoResourceServiceTests')
& ninja -C $build -n @targets *> (Join-Path $run "$Architecture-build-plan.log")
if($LASTEXITCODE){throw 'Focused build plan failed'}
$report=@{schema='fvr.chat06.focused-normal-tests.v1';architecture=$Architecture;source_commit=((& git -C $repo rev-parse HEAD)-join '');started_utc=[DateTime]::UtcNow.ToString('o');passed=$false;build_passed=$false;test_exit=$null;error=$null;targets=$targets;normal_cache_before=$cacheBefore;runtime_receipt=(Pin $priorPath);native_source_sha256=$prior.source_sha256;runtime_rebuilt=$false;native_bc2_verified=$false;headset_tested=$false}
try {
    & cmake --build $build --target @targets --parallel 4 *> (Join-Path $run "$Architecture-build.log")
    if($LASTEXITCODE){throw 'Focused normal test-target build failed'}
    $report.build_passed=$true
    & ctest --test-dir $build --output-on-failure --no-tests=error -R '^(Bc2NativeCycleService|Bc2AmmoResourceService)$' --output-junit (Join-Path $run "$Architecture-tests.xml") *> (Join-Path $run "$Architecture-tests.log")
    $report.test_exit=$LASTEXITCODE
    if($LASTEXITCODE){throw 'Focused normal configuration tests failed'}
    $report.normal_cache_after=Pin $cache
    if($report.normal_cache_after.sha256 -cne $cacheBefore.sha256){throw 'Normal cache changed during focused build'}
    & $python -B $helper verify-source --root $repo --reviewed-header $freeze.reviewed_header.path --receipt $freeze.source_receipt.path --expected $prior.source_sha256 *> (Join-Path $run "$Architecture-source-guard-after.log")
    if($LASTEXITCODE){throw 'Runtime source closure changed during validation'}
    $report.tests=@($targets | ForEach-Object {Pin (Join-Path $build ($_+'.exe'))})
    $report.passed=$true
} catch {$report.error=$_.Exception.Message;throw}
finally {
    $report.completed_utc=[DateTime]::UtcNow.ToString('o')
    Save (Join-Path $run "$Architecture-result.json") $report
    foreach($name in @('build.log','tests.log')){if(Test-Path -LiteralPath (Join-Path $run "$Architecture-$name")){Get-Content -LiteralPath (Join-Path $run "$Architecture-$name") -Tail 12}}
}

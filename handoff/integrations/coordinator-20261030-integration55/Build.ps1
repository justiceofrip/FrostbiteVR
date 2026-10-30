[CmdletBinding()]
param([ValidateSet('Prepare','x86','x64')][string]$Mode='Prepare',[string]$Attempt='01')
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'));$run=Join-Path $repo 'reports/coordinator-20261030-integration55'
$lease=Get-Content -LiteralPath (Join-Path $run 'leases.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Integration lease not owned'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost -ErrorAction SilentlyContinue){throw 'Shared resources not idle'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes $(if($Mode -eq 'Prepare'){16MB}else{3GB}) -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
function Save($name,$value){$path=Join-Path $run $name;if(Test-Path -LiteralPath $path){throw "Preserve $path"};[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))}
function Sha($p){(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant()}
Save "$Mode-$Attempt-storage.json" $budget
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$helper=Join-Path $repo 'tools/bc2_native_operation_receipts.py'
$statePath=Join-Path $run 'source-state.json'
if($Mode -eq 'Prepare'){
 $old=Get-Content -LiteralPath (Join-Path $repo 'reports/coordinator-20261030-headset52/normal-runtime/build-receipt.json') -Raw|ConvertFrom-Json
 foreach($map in @($old.binaries,$old.symbols_by_binary)){foreach($entry in $map.psobject.Properties){if((Sha $entry.Value.path) -ne $entry.Value.sha256){throw 'Fallback artifact changed'}}}
 $proof=Join-Path $run ("provenance-"+$Attempt);$null=[IO.Directory]::CreateDirectory($proof)
 & py -3 -B $helper source --root $repo --output (Join-Path $proof 'source-without-header.json');if($LASTEXITCODE){throw 'Closure failed'}
 $closure=Get-Content -LiteralPath (Join-Path $proof 'source-without-header.json') -Raw|ConvertFrom-Json
 $text=[IO.File]::ReadAllText($old.reviewed_header.path)
 if([regex]::Matches($text,$old.source_sha256).Count -ne 1){throw 'Expected one existing source binding'}
 $header=Join-Path $proof 'Bc2NativeOperationCapabilities.generated.h'
 [IO.File]::WriteAllText($header,$text.Replace($old.source_sha256,$closure.source_sha256),[Text.UTF8Encoding]::new($false))
 $receipt=Join-Path $proof 'source-with-header.json'
 & py -3 -B $helper source --root $repo --reviewed-header $header --output $receipt;if($LASTEXITCODE){throw 'Bound closure failed'}
 Save 'source-state.json' @{source_commit=((& git rev-parse HEAD)-join '');task_commit='84d695244638878590b51377e253660d1c6005c8';source_sha256=$closure.source_sha256;reviewed_header=$header;source_receipt=$receipt;operation_records_changed=$false;headset_accepted=$false;enrollment='profiles/weapons43/Enrollment.cmake'}
 return
}
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
& py -3 -B $helper verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256;if($LASTEXITCODE){throw 'Source changed'}
$vs='C:/Program Files/Microsoft Visual Studio/18/Community';$arch=if($Mode -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation|Out-Null
& cmake -S $repo -B (Join-Path $repo "build/$Mode") -C (Join-Path $repo 'profiles/weapons43/Enrollment.cmake') "-DBC2_NATIVE_OPERATION_CAPABILITIES_HEADER:FILEPATH=$($state.reviewed_header.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_RECEIPT:FILEPATH=$($state.source_receipt.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_SHA256:STRING=$($state.source_sha256)" *> (Join-Path $run "$Mode-$Attempt-configure-binding.log")
if($LASTEXITCODE){Get-Content (Join-Path $run "$Mode-$Attempt-configure-binding.log") -Tail 35;throw 'Binding configuration failed'}
$failure=$null;$started=[DateTime]::UtcNow
try {& (Join-Path $repo 'Build.ps1') -Architecture $Mode -Jobs 4 *> (Join-Path $run "$Mode-$Attempt-build-console.log")}catch{$failure=$_}
foreach($name in @('configure.log','build.log','tests.log','build.json')){
 $p=Join-Path $repo "reports/$Mode-$name"
 if((Test-Path -LiteralPath $p) -and (Get-Item -LiteralPath $p).LastWriteTimeUtc -ge $started -and -not($failure -and $name -eq 'build.json')){Copy-Item -LiteralPath $p -Destination (Join-Path $run "$Mode-$Attempt-$name")}
}
Save "$Mode-$Attempt-result.json" @{source_commit=$state.source_commit;source_sha256=$state.source_sha256;passed=($null -eq $failure);failure=if($failure){$failure.ToString()}else{$null};headset_accepted=$false}
Get-Content (Join-Path $run "$Mode-$Attempt-build-console.log") -Tail 25
if($failure){throw $failure}

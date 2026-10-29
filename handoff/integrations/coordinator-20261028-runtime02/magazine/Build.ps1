#requires -Version 7.5
# Source-only prepared recipe. Root executes after the actual shared normal freeze.
[CmdletBinding()]
param([ValidateSet('Validate','Build')][string]$Mode='Validate',[switch]$Execute,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$NormalReceiptSha256,
 [ValidateRange(1,4)][int]$Jobs=1,[ValidatePattern('^run[0-9]{2}$')][string]$Attempt='run01')
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo='G:\Unity\VRMMO\FrostbiteVR';$build=Join-Path $repo 'build/x86';$out=Join-Path 'G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-runtime02\magazine' ('build01/'+$Attempt)
$python='G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'
$normalPath='G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-batch01\normal-runtime\build-receipt.json'
. (Join-Path $PSScriptRoot 'Common.ps1')
$prep=Assert-Preparation;$normal=Assert-Normal $normalPath $NormalReceiptSha256;$state=Load $normal.source_freeze.path
$overlay=$prep;$requiredCompileTargets=$prep.required_objects
if(@($requiredCompileTargets).Count -ne 3){throw 'Exact reviewed3-object set required'}
CheckPin $workflow.fresh_helper;. $workflow.fresh_helper.path
$sourceWrites=[Collections.Generic.List[object]]::new()
. (Join-Path $repo 'tools/StorageBudget.ps1');$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$env:TMPDIR=$env:TEMP;$env:PYTHONDONTWRITEBYTECODE='1'
# Snapshot the ACTUAL current source set; this does not claim a full historical freeze.
$baselineRows=SourceRows;VerifyRows $baselineRows
foreach($row in $overlay.files){$p=Join-Path $repo $row.source;NoLinks $p
 if($row.before){Check $p $row.before.sha256}elseif(Test-Path -LiteralPath $p){throw 'Private new header already exists'}}
$baselineClosure=NativeClosure ''
if($baselineClosure.source_sha256 -cne $normal.source_sha256 -or -not (SameDataRows $baselineClosure.excluded_data_sha256 (Load $state.source_receipt.path).excluded_data_sha256)){throw 'Native source/data differ from actual normal freeze'}
$normalFlags=$state.normal_flags.x86;VerifyCache (Join-Path $build 'CMakeCache.txt') $normalFlags
foreach($role in 'probe','trace'){Check (Join-Path $build ([IO.Path]::GetFileName($normal.binaries[$role].path))) $normal.binaries[$role].sha256}
$changes=@('BC2_ORDINARY_RESOURCE_INPUT_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE')
$normalExclusions=@('BC2_PLAYER_GRENADES','BC2_PLAYER_CHEST_KNIFE','BC2_PLAYER_CONTEXTUAL_TOOLS')
foreach($k in @('BC2_ORDINARY_MANUAL_CYCLES','BC2_NATIVE_CYCLE_RECOVERY','BC2_RESOURCE_MAGAZINES','BC2_BODY_SCENE_DEPTH')+$normalExclusions){if($normalFlags[$k].value -cne 'ON'){throw ('Required normal feature absent: '+$k)}}
foreach($k in $normalFlags.Keys){if($k -cmatch 'PROBE$' -and $normalFlags[$k].value -cne 'OFF'){throw 'Competing normal finite fixture'}}
# Linker symbols are under symbols/, not the stale root-level PDB names.
$symbolPaths=@{}
$normalGraph=[IO.File]::ReadAllText((Join-Path $build 'build.ninja'))
foreach($target in @('BC2NativeProbe.dll','BC2NativeTrace.exe')){
 $m=[regex]::Matches($normalGraph,('(?ms)^build '+[regex]::Escape($target)+'[^\r\n]*\r?\n(?:(?:  [^\r\n]*)?\r?\n)*?  TARGET_PDB = ([^\r\n]+)'))
 if($m.Count -ne 1){throw ('Actual PDB target missing: '+$target)}
 $p=[IO.Path]::GetFullPath((Join-Path $build $m[0].Groups[1].Value));NoLinks $p
 if(-not $p.StartsWith($build+'\symbols\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($p) -cne '.pdb'){throw 'Unexpected actual symbol path'}
 $symbolPaths[[IO.Path]::GetFileName($p)]=$p
}
$artifactFiles=@(Get-ChildItem -LiteralPath $build -File|Where-Object {$_.Extension -eq '.lib' -or $_.Name -match '^BC2Native(?:Probe|Trace)\.(dll|exe|ilk|exp)$'})+@(foreach($p in $symbolPaths.Values){Get-Item -LiteralPath $p})
$artifactBytes=[long](($artifactFiles|Measure-Object Length -Sum).Sum);if($artifactBytes -gt 512MB){throw 'Preimage allowance exceeded'}
foreach($role in 'probe','trace'){Check $symbolPaths[[IO.Path]::GetFileName($normal.symbols_by_binary[$role].path)] $normal.symbols_by_binary[$role].sha256}
Assert-Lease
if($Mode -eq 'Validate'){@{validated=$true;built=$false;normal=(Pin $normalPath);source_sha256=$normal.source_sha256;required_objects=$requiredCompileTargets;reservation_bytes=3GB;source_overlay_files=@($overlay.files).Count;receiver_reused=$normal.binaries.receiver;raw_maximum_bytes=536870912}|ConvertTo-Json -Depth 5;return}
if(-not $Execute){throw 'Build requires -Execute'}
if(Test-Path -LiteralPath $out){throw 'Preserve previous build attempt'};NoLinks $out
if(Get-Process cl,link,cmake,ninja,MSBuild,ctest,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost -ErrorAction SilentlyContinue){throw 'Build/game/native helper active'}
Assert-Lease
$storage=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $out -Operation Build -PlannedBytes 3GB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$null=[IO.Directory]::CreateDirectory($out);Save 'storage.json' $storage;Save 'source-before.json' @{files=$baselineRows}
$preimageDir=Join-Path $out 'normal-output-preimages';$null=[IO.Directory]::CreateDirectory($preimageDir)
$artifactPins=@(foreach($f in $artifactFiles){$p=Join-Path $preimageDir $f.Name;Copy-Item -LiteralPath $f.FullName -Destination $p;Check $p (Sha $f.FullName);@{destination=$f.FullName;preimage=(Pin $p)}})
Save 'normal-output-preimages.json' @{files=$artifactPins;bytes=$artifactBytes;prunable_only_after_exact_restoration=$true}
foreach($n in 'CMakeCache.txt','build.ninja'){Copy-Item -LiteralPath (Join-Path $build $n) -Destination (Join-Path $out ('before-'+$n))}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'C++ toolchain unavailable'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch x86 -HostArch amd64 -SkipAutomaticLocation|Out-Null

$failure=$null;$restoreErrors=[Collections.Generic.List[string]]::new();$receipt=$null;$configurationAttempted=$false;$normalRebuilt=$false;$normalOutputsRestored=$false;$applied=[Collections.Generic.List[object]]::new()
try {
 Assert-Lease
 foreach($row in $overlay.files){$applied.Add($row);Write-FvrFreshOverlaySource $row.after (Join-Path $repo $row.source) 'apply'}
 $expectedMap=@{};foreach($r in $baselineRows){$expectedMap[$r.path]=$r.sha256};foreach($r in $overlay.files){$expectedMap[$r.source]=$r.after.sha256}
 $expectedRows=@(foreach($k in $expectedMap.Keys){@{path=$k;sha256=$expectedMap[$k]}})
 VerifyRows $expectedRows
 if(-not (SameDataRows (SourceRows) $expectedRows)){throw 'Applied source is not exact reviewed overlay of actual shared normal'}
 $fresh=NativeClosure ''
 if($fresh.source_sha256 -cne $normal.source_sha256){throw 'Flags-only native source changed'}
 $profile=Join-Path $out 'provenance';$null=[IO.Directory]::CreateDirectory($profile)
 $oldText=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($state.reviewed_header.path))
 if([regex]::Matches($oldText,[regex]::Escape($normal.source_sha256)).Count -ne 1){throw 'Expected one reviewed source-hash binding'}
 $header=Join-Path $profile 'Bc2NativeOperationCapabilities.generated.h'
 [IO.File]::WriteAllBytes($header,[Text.Encoding]::Latin1.GetBytes($oldText.Replace($normal.source_sha256,$fresh.source_sha256)))
 $rebound=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($header))
 if($rebound.Replace($fresh.source_sha256,$normal.source_sha256) -cne $oldText){throw 'Operation records changed during source rebinding'}
 $sourceReceipt=Join-Path $profile 'source-with-header.json'
 & $python -B (Join-Path $repo 'tools/bc2_native_operation_receipts.py') source --root $repo --reviewed-header $header --output $sourceReceipt
 if($LASTEXITCODE){throw 'Could not freeze actual diagnostic native source'}
 & $python -B (Join-Path $repo 'tools/bc2_native_operation_receipts.py') verify-source --root $repo --reviewed-header $header --receipt $sourceReceipt --expected $fresh.source_sha256
 if($LASTEXITCODE){throw 'Diagnostic native source/header verification failed'}
 $diagnosticRows=SourceRows;Save 'compiled-source-files.json' @{files=$diagnosticRows}
 $flags=@{};foreach($key in $normalFlags.Keys){$flags[$key]=@{type=$normalFlags[$key].type;value=$normalFlags[$key].value}}
 foreach($key in $changes){if($flags[$key].value -cne 'OFF'){throw 'Unexpected normal diagnostic feature'};$flags[$key].value='ON'}
 foreach($key in $normalExclusions){if($flags[$key].value -cne 'ON'){throw 'Expected normal feature before finite-diagnostic exclusion'};$flags[$key].value='OFF'}

 $flags.BC2_NATIVE_OPERATION_CAPABILITIES_HEADER.value=$header.Replace('\','/')
 $flags.BC2_NATIVE_OPERATION_SOURCE_RECEIPT.value=$sourceReceipt.Replace('\','/')
 $flags.BC2_NATIVE_OPERATION_SOURCE_SHA256.value=$fresh.source_sha256
 Save 'composition.json' @{normal=(Pin $normalPath);source_preparation=(Pin (Join-Path $PSScriptRoot 'source-preparation.json'));native_source_sha256=$fresh.source_sha256;normal_flags=$normalFlags;diagnostic_flags=$flags;operation_records_changed=$false;enabled_diagnostic_flags=$changes;excluded_normal_features=$normalExclusions;required_objects=$requiredCompileTargets;targets=@('BC2NativeProbe','BC2NativeTrace');tests_repeated=$false}
 $configurationAttempted=$true;Configure $flags 'diagnostic-configure.log'
 $graph=Get-Content -LiteralPath (Join-Path $build 'build.ninja') -Raw
 $compileRows=[Collections.Generic.List[object]]::new();$compileProofFailure=$null
 $expected=@($changes|ForEach-Object {'FVR_'+$_}|Sort-Object)
 foreach($file in 'NativeProbe.cpp','Bc2Gameplay.cpp'){
  $pattern='(?m)^build CMakeFiles\\BC2NativeProbe\.dir\\src\\games\\bc2\\'+[regex]::Escape($file)+'\.obj:[^\r\n]*\r?\n  CONFIG = RelWithDebInfo\r?\n  DEFINES = ([^\r\n]+)'
  $match=[regex]::Matches($graph,$pattern)
  if($match.Count -ne 1){$compileRows.Add(@{file=$file;compile_rows=$match.Count;expected=$expected;actual=@();passed=$false});$compileProofFailure="Missing actual compile row: $file";continue}
  $defines=$match[0].Groups[1].Value
  $tokens=@([regex]::Matches($defines,'(?:^|\s)-D(FVR_BC2_[A-Z0-9_]+)(?:=[^\s]*)?(?=\s|$)')|ForEach-Object {$_.Groups[1].Value}|Sort-Object -Unique)
  $actual=@($tokens|Where-Object {$_ -cmatch 'PROBE$'})
  $normalConsumers=@($tokens|Where-Object {$_ -cin @('FVR_BC2_PLAYER_GRENADE_NORMAL','FVR_BC2_PLAYER_KNIFE_NORMAL','FVR_BC2_PLAYER_CONTEXTUAL_NORMAL')})
  $pass=($actual -join '|') -ceq ($expected -join '|') -and $normalConsumers.Count -eq 0
  $compileRows.Add(@{file=$file;compile_rows=1;expected=$expected;actual=$actual;normal_consumers=$normalConsumers;all_bc2_tokens=$tokens;defines=$defines;passed=$pass})
  if(-not $pass){$compileProofFailure="Competing/missing actual diagnostic definitions: $file"}
 }
 Save 'compiler-definition-proof.json' @{checked_before_compile=$true;rows=@($compileRows);passed=($null -eq $compileProofFailure);failure=$compileProofFailure}
 if($compileProofFailure){throw $compileProofFailure}
 BuildTargets 'diagnostic-build.log'
 VerifyRows $diagnosticRows;VerifyCache (Join-Path $build 'CMakeCache.txt') $flags
 $proof=OperationProof
 foreach($n in 'CMakeCache.txt','build.ninja'){Copy-Item -LiteralPath (Join-Path $build $n) -Destination (Join-Path $out $n)}
 Copy-Item -LiteralPath (Join-Path $build 'generated/bc2-operation/Bc2NativeOperationBuild.generated.h') -Destination (Join-Path $out 'Bc2NativeOperationBuild.generated.h')
 $binaries=@{receiver=$normal.binaries.receiver};$symbols=@{}
 foreach($row in @(@('probe','BC2NativeProbe.dll'),@('trace','BC2NativeTrace.exe'))){$dest=Join-Path $out $row[1];Copy-Item -LiteralPath (Join-Path $build $row[1]) -Destination $dest;Check $dest (Sha (Join-Path $build $row[1]));$binaries[$row[0]]=Pin $dest}
 foreach($name in 'BC2NativeProbe.pdb','BC2NativeTrace.pdb'){$dest=Join-Path $out $name;Copy-Item -LiteralPath $symbolPaths[$name] -Destination $dest;Check $dest (Sha $symbolPaths[$name]);$symbols[$name]=Pin $dest}
 $frozenBytes=[long](($binaries.Values|Where-Object path -Like ($out+'*')|Measure-Object bytes -Sum).Sum)+[long](($symbols.Values|Measure-Object bytes -Sum).Sum)
 if($frozenBytes -gt 512MB){throw 'Diagnostic runtime/symbols exceed frozen-payload allowance'}

 $sourceMap=@{};foreach($r in $diagnosticRows){$sourceMap[$r.path]=$r.sha256}
 $overrides=@{};foreach($r in $overlay.files){$overrides[$r.source]=$r.after}
 Assert-Lease
 $receipt=@{schema='fvr.r56.generic-single-magazine.build.v1';source_root=$repo;source_files=$sourceMap;source_overrides=$overrides;normal=$false;diagnostic_only=$true;diagnostic_kind='ordinary-input-single-magazine-return-replace-support-postfire';normal_baseline=(Pin $normalPath);source_preparation=(Pin (Join-Path $PSScriptRoot 'source-preparation.json'));workflow=(Pin (Join-Path $PSScriptRoot 'workflow-preparation.json'));full_source_manifest=(Pin (Join-Path $out 'compiled-source-files.json'));source_sha256=$fresh.source_sha256;source_receipt=(Pin $sourceReceipt);reviewed_header=(Pin $header);operation_provenance=$proof;binaries=$binaries;symbols=$symbols;cmake_cache=(Pin (Join-Path $out 'CMakeCache.txt'));compiler_graph=(Pin (Join-Path $out 'build.ninja'));generated_operation_header=(Pin (Join-Path $out 'Bc2NativeOperationBuild.generated.h'));runner=(Pin $PSCommandPath);ordinary_resource_input_probe=$true;ordinary_manual_cycles=$true;resource_magazines=$true;native_cycle_recovery=$true;flags_only=$true;two_hand_empty=$false;empty_magazine=$false;single_magazine=$true;campaign_magazine=$true;combined_pump=$false;boat_exit_reload_holster=$false;ordinary_bolt_input_probe=$false;manual_bolt_probe=$false;m95_stock_shot_probe=$false;manual_pump_probe=$false;resource_pump_probe=$false;resource_inventory_probe=$false;ammo_move_probe=$false;ammo_refill_probe=$false;ammo_resource_hands=$false;ordinary_resource_interruption_probe=$false;operation_records_changed=$false;raw_maximum_bytes=536870912;required_object_count=3;required_objects=$requiredCompileTargets;headset_verified=$false;game_executed=$false;tests_executed=$false;storage=$storage}

}catch{$failure=$_.Exception.Message}
finally {
 foreach($row in $applied){try {
  $p=Join-Path $repo $row.source;NoLinks $p
  if(Test-Path -LiteralPath $p){$h=Sha $p;if($h -cne $row.after.sha256 -and (-not $row.before -or $h -cne $row.before.sha256)){throw 'Concurrent source change; refuse overwrite'}}
  if($row.before){Write-FvrFreshOverlaySource $row.before $p 'restore'}
  elseif(Test-Path -LiteralPath $p){if([IO.Path]::GetFullPath($p) -ine [IO.Path]::GetFullPath((Join-Path $repo 'src/games/bc2/Bc2BoatExitSequence.h'))){throw 'Unexpected new source cleanup target'};Remove-Item -LiteralPath $p}
 }catch{$restoreErrors.Add($_.Exception.Message)}}
 Save 'source-write-evidence.json' @{writes=@($sourceWrites);copy_timestamps_not_reused=$true}
 try {VerifyRows $baselineRows;$restoredClosure=NativeClosure '';if($restoredClosure.source_sha256 -cne $normal.source_sha256){throw 'Restored native closure differs'}}catch{$restoreErrors.Add($_.Exception.Message)}
 if($configurationAttempted -and -not $restoreErrors.Count){try {
   Configure $normalFlags 'restore-normal-configure.log';BuildTargets 'restore-normal-targets.log'
   $normalProof=OperationProof;Save 'restored-normal-operation-proof.json' $normalProof
   VerifyRows $baselineRows;VerifyCache (Join-Path $build 'CMakeCache.txt') $normalFlags;$normalRebuilt=$true
  }catch{$restoreErrors.Add($_.Exception.Message)}}
 if(-not $restoreErrors.Count){try {
   foreach($p in $artifactPins){Assert-Lease;CheckPin $p.preimage;Copy-Item -LiteralPath $p.preimage.path -Destination $p.destination;Check $p.destination $p.preimage.sha256}
   foreach($name in 'CMakeCache.txt','build.ninja'){Assert-Lease;Copy-Item -LiteralPath (Join-Path $out ('before-'+$name)) -Destination (Join-Path $build $name);Check (Join-Path $build $name) (Sha (Join-Path $out ('before-'+$name)))}
   VerifyCache (Join-Path $build 'CMakeCache.txt') $normalFlags
   foreach($pin in $normal.symbols_by_binary.Values){CheckPin $pin}
   $normalOutputsRestored=$true
   foreach($p in $normal.binaries.Values){CheckPin $p}
  }catch{$restoreErrors.Add($_.Exception.Message)}}
 Save 'completion.json' @{built=($null -ne $receipt -and -not $failure -and -not $restoreErrors.Count);failure=$failure;restore_errors=@($restoreErrors);normal_source_restored=(-not $restoreErrors.Count);normal_configuration_restored=(-not $restoreErrors.Count);normal_targets_rebuilt=$normalRebuilt;normal_outputs_exactly_restored=$normalOutputsRestored;frozen_normal_runtime_unchanged=(-not $restoreErrors.Count);scope='Nambu magazine only; not CHAT07 combined interactions or installed-runtime acceptance';tests_executed=$false;game_executed=$false;installed=$false;preimage_bytes=$artifactBytes;diagnostic_source_retained_in_overlay=$false}
}
if($failure -or $restoreErrors.Count){throw "Diagnostic build/restoration failed: $failure; $($restoreErrors -join '; ')"}
$receipt.normal_configuration_restored=$true;$receipt.normal_outputs_exactly_restored=$true
$receipt.source_write_evidence=Pin (Join-Path $out 'source-write-evidence.json')
$receipt.diagnostic_object_compilation=Pin (Join-Path $out 'diagnostic-object-compilation-proof.json')
$receipt.normal_object_compilation=Pin (Join-Path $out 'restore-normal-object-compilation-proof.json')
$receipt.fresh_overlay_helper=Pin (Join-Path $PSScriptRoot 'FreshOverlay.ps1')
$receipt.source_before=Pin (Join-Path $out 'source-before.json')
$receipt.composition=Pin (Join-Path $out 'composition.json')
$receipt.compiler_definition=Pin (Join-Path $out 'compiler-definition-proof.json')
$receipt.scope='Nambu magazine only; not CHAT07 combined interactions or installed-runtime acceptance'
$receipt.source_commit_claim=$false
Save 'build-receipt.json' $receipt
# Delete only these verified duplicate output backups after their exact bytes
# are restored in the main tree. Never recurse; retain source/config receipts.
foreach($p in $artifactPins){Check $p.destination $p.preimage.sha256;CheckPin $p.preimage
 $target=[IO.Path]::GetFullPath($p.preimage.path);$parent=[IO.Path]::GetFullPath($preimageDir)+'\'
 if(-not $target.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Preimage cleanup escaped owned directory'}
 Assert-Lease;Remove-Item -LiteralPath $target
}
Save 'duplicate-preimages-retired.json' @{retired=$true;bytes=$artifactBytes;normal_outputs_verified=$true;only_files_from_normal_output_preimages_receipt=$true}
Pin (Join-Path $out 'build-receipt.json')|ConvertTo-Json

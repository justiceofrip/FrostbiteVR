# Batch01 admission for the existing finite SingleMagazine recipe. No native overlays.
$script:RecipeRoot=$PSScriptRoot
$script:EvidenceRoot='G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-runtime02\magazine'
$script:NormalPath='G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-batch01\normal-runtime\build-receipt.json'
$script:NormalSha='a196a0b16622fb85216bad8a9bfdf7598fadea37db20dc050284e0b456004fe4'
$script:NativeSha='d74a6b63c6360d77007c46d1f3c9f8ca342ce1740466688a7ffd6ab43d3b26ed'
$script:NormalCommit='6e5fc12d3b98c5065b2bb8d385d28612e4c1c146'
$ancestor='G:\Unity\VRMMO\bc2vr-recovery\performance436\generic-magazine-r56-prep\Common.ps1'
if((Get-FileHash -LiteralPath $ancestor).Hash.ToLowerInvariant() -cne '144d788a39ed468c250c0f933644cda8bd1fb3f55257fec4c45cd3398062af9e'){throw 'Pinned generic Common changed'}
. $ancestor

function Assert-Lease {
 $lease=Load 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
 if($lease.owner_token -cne 'campaign-coordinator-20261028-r1-runtime02-91aa4c54d34a41219b8483ccdd7629cb' -or $lease.run -cne 'coordinator-20261028-runtime02'){throw 'Exact runtime02 integration lease required'}
}
function Assert-Normal($path,$sha){
 if([IO.Path]::GetFullPath($path) -ine $NormalPath -or $sha -cne $NormalSha){throw 'Exact batch01 normal receipt required'}
 Check $path $sha;$n=Load $path
 if($n.schema -cne 'fvr.checkpoint436.normal-build.v1' -or $n.version -cne 'unreleased-coordinator-20261028-batch01' -or $n.built_source_commit -cne $NormalCommit -or $n.prepared_source_commit -cne $NormalCommit -or $n.source_sha256 -cne $NativeSha -or $n.normal -ne $true -or $n.full_normal_builds -ne $true -or $n.diagnostic_only -ne $false -or $n.operation_records_changed -ne $false){throw 'Batch01 normal identity differs'}
 Check $n.source_receipt $n.source_receipt_sha256
 foreach($pin in @($n.source_freeze,$n.reviewed_header,$n.symbol_audit,$n.native_provenance_baseline,$n.fallback_verification)+@($n.binaries.Values)+@($n.symbols_by_binary.Values)){CheckPin $pin}
 foreach($arch in 'x86','x64'){CheckPin $n.normal_cache[$arch].cache;CheckPin $n.normal_cache[$arch].operation_config}
 $freeze=Load $n.source_freeze.path
 if($freeze.schema -cne 'fvr.chat25-chat26.normal-source-freeze.v1' -or $freeze.source_commit -cne $NormalCommit -or $freeze.source_sha256 -cne $NativeSha -or $freeze.operation_records_changed -ne $false -or $freeze.source_receipt.path -ine $n.source_receipt -or $freeze.source_receipt.sha256 -cne $n.source_receipt_sha256 -or $freeze.reviewed_header.sha256 -cne $n.reviewed_header.sha256){throw 'Batch01 source freeze differs'}
 foreach($pin in @($freeze.preparation,$freeze.runner,$freeze.reviewed_header,$freeze.source_receipt)){CheckPin $pin}
 $source=Load $n.source_receipt
 if($source.source_sha256 -cne $NativeSha -or $source.reviewed_header_sha256 -cne $n.reviewed_header.sha256 -or $source.native_admission_granted -ne $false){throw 'Batch01 native provenance differs'}
 $seen=@()
 foreach($v in $n.validation){
  if($v.architecture -cnotin @('x86','x64') -or $v.architecture -cin $seen -or $v.cpp_suite_count -ne 305){throw 'Distinct 305-suite normal results required'}
  foreach($pin in $v.evidence.Values){CheckPin $pin}
  $result=Load $v.evidence['result.json'].path;$built=Load $v.evidence['build.json'].path;$focused=Load $v.evidence['focused-result.json'].path
  if($result.passed -ne $true -or $result.focused_passed -ne $true -or $result.failure -or $result.source_commit -cne $NormalCommit -or $result.source_sha256 -cne $NativeSha -or $built.architecture -cne $v.architecture -or $built.built -ne $true -or $built.tests_passed -ne $true -or $focused.passed -ne $true -or $focused.suite_count -ne 4 -or $focused.architecture -cne $v.architecture -or $focused.source_commit -cne $NormalCommit -or $focused.source_sha256 -cne $NativeSha){throw 'Batch01 completed validations differ'}
  $log=[IO.File]::ReadAllText($v.evidence['tests.log'].path)
  if($log -notmatch '100% tests passed, 0 tests failed out of 305'){throw 'Actual 305-suite log proof missing'}
  $seen+=@($v.architecture)
 }
 if($seen.Count -ne 2 -or $n.binaries.Count -ne 6 -or $n.symbols_by_binary.Count -ne 6){throw 'Both architectures and six binary/symbol pairs required'}
 return $n
}
function Assert-Preparation {
 Assert-Lease
 $script:workflow=Load (Join-Path $RecipeRoot 'workflow-preparation.json')
 if($workflow.schema -cne 'fvr.runtime02.magazine.workflow.v1' -or $workflow.normal.sha256 -cne $NormalSha){throw 'Exact reviewed runtime02 workflow required'}
 foreach($pin in @($workflow.files)+@($workflow.workflow_sources)+@($workflow.source,$workflow.fresh_helper,$workflow.python_manifest)){CheckPin $pin}
 $p=Load $workflow.source.path
 if($p.schema -cne 'fvr.runtime02.magazine.source.v1' -or $p.flags_only -ne $true -or @($p.files).Count -ne 0 -or @($p.required_objects).Count -ne 3 -or $p.normal_source_sha256 -cne $NativeSha){throw 'Zero-overlay current-native recipe required'}
 foreach($pin in $p.source_proof){CheckPin $pin}
 foreach($f in (Load $workflow.python_manifest.path).files){if($f.path.StartsWith('python/')){Check (Join-Path (Split-Path $workflow.python_manifest.path) $f.path) $f.sha256}}
 return $p
}
function Configure($flags,$log){
 Assert-Lease
 $args=@('-S',$repo,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DBUILD_TESTING=ON')
 foreach($key in ($flags.Keys|Sort-Object)){$args+=('-D'+$key+':'+$flags[$key].type+'='+$flags[$key].value)}
 & cmake @args *> (Join-Path $out $log);if($LASTEXITCODE){throw "Configuration failed: $log"}
 Assert-Lease;VerifyCache (Join-Path $build 'CMakeCache.txt') $flags
}
function BuildTargets($log){Assert-Lease;Invoke-FvrFreshTargetBuild $log;Assert-Lease}
function Assert-CompletedBuild($path,$sha){
 Assert-Lease;Check $path $sha;$b=Load $path;$dir=Split-Path $path
 if([IO.Path]::GetFullPath((Split-Path $dir)) -ine (Join-Path $EvidenceRoot 'build01') -or (Split-Path $dir -Leaf) -cnotmatch '^run[0-9]{2}$'){throw 'Exact runtime02 build directory required'}
 if($b.schema -cne 'fvr.r56.generic-single-magazine.build.v1' -or $b.source_preparation.sha256 -cne $workflow.source.sha256 -or $b.workflow.sha256 -cne (Sha (Join-Path $RecipeRoot 'workflow-preparation.json')) -or $b.required_object_count -ne 3 -or $b.raw_maximum_bytes -ne 536870912){throw 'Wrong finite SingleMagazine receipt'}
 foreach($k in 'diagnostic_only','flags_only','campaign_magazine','single_magazine','ordinary_resource_input_probe','ordinary_manual_cycles','native_cycle_recovery','resource_magazines','normal_outputs_exactly_restored','normal_configuration_restored'){if($b[$k] -ne $true){throw "Missing build proof: $k"}}
 foreach($k in 'normal','two_hand_empty','empty_magazine','combined_pump','boat_exit_reload_holster','ordinary_bolt_input_probe','manual_bolt_probe','manual_pump_probe','resource_pump_probe','resource_inventory_probe','ammo_move_probe','ammo_refill_probe','ammo_resource_hands','ordinary_resource_interruption_probe','operation_records_changed'){if($b[$k] -ne $false){throw "Competing build mode: $k"}}
 CheckPin $b.normal_baseline;$n=Assert-Normal $b.normal_baseline.path $b.normal_baseline.sha256
 if($b.source_root -ine $repo -or $b.source_sha256 -cne $NativeSha -or $b.source_overrides.Count -ne 0 -or $b.runner.sha256 -cne (Sha (Join-Path $RecipeRoot 'Build.ps1'))){throw 'Runtime02 build identity differs'}
 $c=Load (Join-Path $dir 'completion.json')
 if($c.built -ne $true -or $c.failure -or @($c.restore_errors).Count){throw 'Build/restoration failed'}
 foreach($k in 'normal_source_restored','normal_configuration_restored','normal_targets_rebuilt','normal_outputs_exactly_restored','frozen_normal_runtime_unchanged'){if($c[$k] -ne $true){throw "Missing restoration: $k"}}
 foreach($pin in @($b.runner,$b.source_preparation,$b.workflow,$b.full_source_manifest,$b.source_receipt,$b.reviewed_header,$b.cmake_cache,$b.compiler_graph,$b.generated_operation_header,$b.source_write_evidence,$b.fresh_overlay_helper,$b.source_before,$b.composition,$b.compiler_definition)){CheckPin $pin}
 Check $b.reviewed_header.path $n.reviewed_header.sha256
 Check $b.generated_operation_header.path $n.normal_cache.x86.operation_config.sha256
 $native=Load $b.source_receipt.path;$normalNative=Load $n.source_receipt
 if($native.source_sha256 -cne $NativeSha -or -not (SameDataRows $native.files $normalNative.files) -or -not (SameDataRows $native.excluded_data_sha256 $normalNative.excluded_data_sha256)){throw 'Actual native source/data changed'}
 $rows=(Load $b.full_source_manifest.path).files
 if(-not (SameDataRows $rows @($b.source_files.GetEnumerator()|ForEach-Object {@{path=$_.Key;sha256=$_.Value}}))){throw 'Actual current source map differs'}
 $before=Load $b.source_before.path
 if(-not (SameDataRows $rows $before.files) -or @((Load $b.source_write_evidence.path).writes).Count){throw 'Zero-overlay source preservation missing'}
 $prep=Load $workflow.source.path
 foreach($phase in @(@('diagnostic_object_compilation','diagnostic'),@('normal_object_compilation','restore-normal'))){
  CheckPin $b[$phase[0]];$proof=Load $b[$phase[0]].path;CheckPin $proof.log
  if($proof.phase -cne $phase[1] -or $proof.passed -ne $true -or $proof.failure -or @($proof.required_objects).Count -ne 3){throw 'Actual three-consumer compilation proof missing'}
  foreach($target in $prep.required_objects){
   $match=@($proof.required_objects|Where-Object {$_.object -ceq $target.object -and $_.source.path -ieq (Join-Path $repo $target.source)})
   if($match.Count -ne 1 -or -not $match[0].compile_command -or $match[0].source.sha256 -cne $b.source_files[$target.source]){throw 'Actual consumer source/compile mismatch'}
  }
 }
 $composition=Load $b.composition.path
 if($composition.operation_records_changed -ne $false -or $composition.native_source_sha256 -cne $NativeSha -or ($composition.enabled_diagnostic_flags -join '|') -cne ($prep.enabled_diagnostic_flags -join '|') -or ($composition.excluded_normal_features -join '|') -cne ($prep.excluded_normal_features -join '|')){throw 'Diagnostic flag delta differs'}
 $normalFlags=(Load $n.source_freeze.path).normal_flags.x86
 $expected=@{};foreach($key in $normalFlags.Keys){$expected[$key]=@{type=$normalFlags[$key].type;value=$normalFlags[$key].value}}
 foreach($key in $prep.enabled_diagnostic_flags){$expected[$key].value='ON'}
 foreach($key in $prep.excluded_normal_features){$expected[$key].value='OFF'}
 $expected.BC2_NATIVE_OPERATION_CAPABILITIES_HEADER.value=$b.reviewed_header.path.Replace('\','/')
 $expected.BC2_NATIVE_OPERATION_SOURCE_RECEIPT.value=$b.source_receipt.path.Replace('\','/')
 VerifyCache $b.cmake_cache.path $expected
 $definition=Load $b.compiler_definition.path
 if($definition.passed -ne $true -or $definition.failure -or @($definition.rows).Count -ne 2){throw 'Actual finite compiler definitions missing'}
 foreach($pin in @($b.binaries.Values)+@($b.symbols.Values)){CheckPin $pin}
 if($b.binaries.receiver.path -cne $n.binaries.receiver.path -or $b.binaries.receiver.sha256 -cne $n.binaries.receiver.sha256 -or $b.fresh_overlay_helper.sha256 -cne $workflow.fresh_helper.sha256){throw 'Receiver/rebuild helper differs'}
 return $b
}

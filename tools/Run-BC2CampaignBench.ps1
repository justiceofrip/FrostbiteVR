#requires -Version 7.5
# Private bench: Validate is nonmutating. Every native mode additionally needs -Execute.
[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Plan,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$PlanSha256,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$DefinitionId,
 [ValidateSet('Validate','Observe','Equip','Restore','Emulate')][string]$Mode='Validate',
 [switch]$Execute,
 [string]$GameReceipt,[string]$GameReceiptSha256,
 [string]$ResourceReport,[string]$ResourceReportSha256,
 [string]$PriorJob,[string]$PriorJobSha256,
 [string]$DiagnosticBuildReceipt,[string]$DiagnosticBuildReceiptSha256,
 [string]$Python,[string]$GeometryCache,[string]$GeometrySha256,
 [ValidateSet('SingleMagazine','CombinedPump')][string]$EmulationEpisode='SingleMagazine',
 [switch]$AllowNativeImages,
 [ValidateRange(1,4096)][int]$CaptureWidth=1920,[ValidateRange(1,4096)][int]$CaptureHeight=1080,
 [string]$Output
)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repository=Split-Path -Parent $PSScriptRoot
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load($path){Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable}
function Pin($path){@{path=[IO.Path]::GetFullPath($path);sha256=(Sha $path);bytes=(Get-Item -LiteralPath $path).Length}}
function Check($path,$hash){if($hash -notmatch '^[a-f0-9]{64}$' -or (Sha $path) -cne $hash){throw ('Pinned input changed: '+$path)}}
function CheckPin($p){Check $p.path $p.sha256;if($p.ContainsKey('bytes') -and (Get-Item -LiteralPath $p.path).Length -ne $p.bytes){throw 'Pinned file length changed'}}
function NoLinks($path){for($at=[IO.Path]::GetFullPath($path);$at;$at=[IO.Path]::GetDirectoryName($at)){if((Test-Path -LiteralPath $at) -and ((Get-Item -LiteralPath $at -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw ('Linked path refused: '+$at)}}}
function Save($name,$value){$path=Join-Path $Output $name;if(Test-Path -LiteralPath $path){throw 'Preserve existing bench evidence'};[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 45),[Text.UTF8Encoding]::new($false))}
Check $Plan $PlanSha256;$planData=Load $Plan
if($planData.schema -cne 'fvr.bc2.campaign_bench_plan.v1' -or -not $planData.planning_only -or $planData.runtime_admission_granted){throw 'Expected nonmutating exact bench plan'}
foreach($p in $planData.inputs){CheckPin $p};CheckPin $planData.planner
$selected=@($planData.rows|Where-Object {$_.id -ceq $DefinitionId})
if($selected.Count -ne 1 -or $DefinitionId -cnotin $planData.loadout){throw 'Select one exact planned loadout configuration'}
$row=$selected[0]
if($row.availability -cne 'mounted_candidate'){throw 'Ambiguous/missing mount evidence; use a matching existing map and new catalog'}
$binding=$planData.provisioning;$variant=$null
if($binding){
 foreach($p in @($binding.build,$binding.prepared_inputs,$binding.header,$binding.runner,$binding.dll)+@($binding.source_pins)){CheckPin $p}
 $variants=@($binding.variants|Where-Object {$_.name -ceq $row.name -and ($_.path+'.dbx') -ieq $row.resource})
 if($variants.Count -eq 1){$variant=$variants[0]}
}
if($Mode -eq 'Validate'){
 @{schema='fvr.bc2.campaign_bench.validation.v1';definition=$row.id;coverage=$row.coverage;mechanism=$row.mechanism;provision_variant=$variant;native_execution=$false;emulation_requires_compatible_diagnostic_build=$true;game_not_opened=$true}|ConvertTo-Json -Depth 5
 return
}
if(-not $Execute){throw 'Native modes require explicit -Execute; Validate is the default'}
if($Mode -eq 'Emulate' -and -not $AllowNativeImages){throw 'Native diagnostic has eight bounded raw frames; explicit -AllowNativeImages and capture dimensions required, otherwise no session starts'}
if($Mode -ne 'Emulate' -and -not $variant){throw 'No exact compiled provisioning variant; do not guess a numeric selector'}
Check $GameReceipt $GameReceiptSha256;$identity=Load $GameReceipt
if(-not $identity.pid -or -not $identity.creation_filetime -or -not $identity.path){throw 'Exact PID, creation FILETIME and executable path required'}
$gameExe=$identity.path;Check $gameExe '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
function Game {
 $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
 if($games.Count -ne 1 -or $games[0].Id -ne $identity.pid -or $games[0].Path -ine $gameExe -or [string]$games[0].StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$identity.creation_filetime){throw 'Game lifetime/path changed'}
 return $games[0]
}
function Idle {
 if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,ProvisionRunner,cl,link,ninja,cmake,MSBuild -ErrorAction SilentlyContinue){throw 'Another BC2 session or build is active'}
}
$game=Game;Idle
if(@($game.Modules|Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count){throw 'Fresh game required; a retained VR module must be independently drained before another bench operation'}
$Output=[IO.Path]::GetFullPath($Output);NoLinks $Output
if([IO.Path]::GetPathRoot($Output) -ine 'G:\' -or (Test-Path -LiteralPath $Output) -or -not (Test-Path -LiteralPath (Split-Path $Output))){throw 'New private output folder under an existing G: parent required'}
if($Output.StartsWith(([IO.Path]::GetDirectoryName($gameExe)+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Bench output cannot be in the game installation'}
$prior=$null;$priorNative=$null
if($PriorJob){
 Check $PriorJob $PriorJobSha256;$prior=Load $PriorJob
 if($prior.schema -cne 'fvr.bc2.campaign_bench.provision-job.v1' -or -not $prior.complete -or $prior.plan_sha256 -cne $PlanSha256 -or $prior.definition -cne $DefinitionId -or $prior.pid -ne $identity.pid -or [string]$prior.creation_filetime -cne [string]$identity.creation_filetime){throw 'Prior job identity/cleanup differs'}
 CheckPin $prior.native_report;CheckPin $prior.bootstrap_report;$priorNative=Load $prior.native_report.path
 if(-not $priorNative.complete -or -not $priorNative.hooks_removed -or -not $priorNative.code_restored){throw 'Prior native job not drained'}
}
if($Mode -eq 'Equip' -and (-not $prior -or $prior.mode -cne 'Observe' -or $priorNative.native_calls -ne 0 -or $priorNative.preflight_passes -lt 8 -or $priorNative.server_observations -lt 8 -or $priorNative.seat_thread -ne $priorNative.server_thread)){throw 'Equip requires the same successful native Observe preflight'}
if($Mode -eq 'Restore' -and (-not $prior -or $prior.mode -cne 'Equip' -or -not $priorNative.client_server_converged)){throw 'Restore requires the exact completed retained Equip'}
if($Mode -eq 'Observe' -and $PriorJob){throw 'Observe does not resume a prior transaction'}
$asset=0;$data=0
if($Mode -eq 'Observe'){
 Check $ResourceReport $ResourceReportSha256;$resources=Load $ResourceReport
 if($resources.schema -cne 'fvr.bc2.readonly_selected_weapon_resources.v1' -or $resources.pid -ne $identity.pid -or -not $resources.read_only -or $resources.native_functions_called -ne $false -or $resources.inventory_modified -ne $false -or $resources.executable_sha256 -cne (Sha $gameExe)){throw 'Exact read-only loaded-resource evidence required'}
 $matches=@($resources.scan.accepted|Where-Object {$_.name -ceq $row.name -and ($_.path+'.dbx') -ieq $row.resource -and $_.authored_configuration_sha256 -ceq $row.resource_sha256 -and $_.asset.address -gt 65536 -and $_.asset.object -eq $_.data})
 if($matches.Count -ne 1){throw 'Require one exact typed loaded Asset/Data pair, not a name-only match'}
 $asset=$matches[0].asset.address;$data=$matches[0].data
}elseif($Mode -eq 'Equip'){$asset=$priorNative.target_asset;$data=$priorNative.target_data}
# Emulation reuses the existing bounded ordinary-controller driver. It is a
# selected magazine episode or the existing combined pump recipe. Neither
# enrolls unsupported weapons or grants native receipt acceptance.
$build=$null
if($Mode -eq 'Emulate'){
 if($row.mechanism -cne 'magazine' -or $row.coverage -cne 'configured_metadata'){throw 'This emulation recipe requires an exactly configured magazine weapon'}
 Check $DiagnosticBuildReceipt $DiagnosticBuildReceiptSha256;$build=Load $DiagnosticBuildReceipt
 if($planData.runtime -and $planData.runtime.sha256 -cne $DiagnosticBuildReceiptSha256){throw 'The plan names a different runtime receipt; create a plan for this diagnostic build'}
 if(-not $build.ordinary_resource_input_probe -or -not $build.ordinary_manual_cycles -or -not $build.resource_magazines -or $build.ordinary_resource_interruption_probe -or $build.ordinary_resource_scene_probe -or $build.pump_receiver_gap_probe -or $build.shell_focus_loss_probe){throw 'One ordinary magazine/SPAS fixture with no overlapping scenario required'}
 foreach($p in $build.source_files.GetEnumerator()){Check (Join-Path $build.source_root $p.Key) $p.Value}
 foreach($role in 'probe','trace','receiver'){CheckPin $build.binaries[$role]}
 Check $GeometryCache $GeometrySha256
 if((Get-Item -LiteralPath $GeometryCache).Length -gt $planData.asset_budget.maximum_bytes){throw 'Body cache exceeds the unchanged size limit'}
 $cacheBytes=[IO.File]::ReadAllBytes($GeometryCache)
 if($cacheBytes.Length -gt $planData.asset_budget.maximum_bytes -or $cacheBytes.Length -lt 12 -or [Text.Encoding]::ASCII.GetString($cacheBytes,0,8) -cne 'BC2PROP1' -or [BitConverter]::ToUInt32($cacheBytes,8) -gt $planData.asset_budget.maximum_records){throw 'Body cache exceeds the unchanged 16 MiB/64-record limits'}
 if(-not $Python -or -not (Test-Path -LiteralPath $Python -PathType Leaf)){throw 'Explicit existing Python runtime required'}
 $baseScript=Join-Path $repository 'tools/Run-OrdinaryResourceInputProbe.ps1'
 if([bool]$build.campaign_magazine -ne ($EmulationEpisode -eq 'SingleMagazine')){throw 'Requested episode differs from compiled diagnostic composition'}
}
if($Mode -ne 'Emulate'){
 $retained=@($game.Modules|Where-Object {$_.ModuleName -like 'BC2ProvisionHarness*'})
 if($Mode -eq 'Observe' -and $retained.Count){throw 'Observe requires a fresh provisioning lifetime'}
 if($Mode -in @('Equip','Restore') -and ($retained.Count -ne 1 -or $retained[0].FileName -ine $binding.dll.path)){throw 'Same retained provisioning DLL required'}
}
. (Join-Path $repository 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repository
# The native collector can capture a larger RTV than the desktop client.
# Match its existing eight-frame, 4096-square RGBA hard limit.
$rawBytes=if($Mode -eq 'Emulate'){[long]8*4096*4096*4}else{0}
$storage=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $Output -Operation Session -PlannedBytes (256MB+$rawBytes) -RawCaptureBytes $rawBytes
$null=[IO.Directory]::CreateDirectory($Output);Save 'storage.json' $storage
Save 'request.json' @{plan=(Pin $Plan);definition=$DefinitionId;mode=$Mode;game=(Pin $GameReceipt);os_input=$false;raw_images=($rawBytes -gt 0);raw_capture_budget=$rawBytes;episode=$EmulationEpisode;retry=$false;world_spawn=$false;ammo_rollback=$false}
$failure=$null;$completed=$false;$exitCode=$null
try {
 $game=Game;Idle
 if($Mode -eq 'Emulate'){
  if($EmulationEpisode -eq 'CombinedPump'){
  $inventoryPath=Join-Path $Output 'inventory-before.json'
  & $Python -B (Join-Path $build.source_root 'tools/capture_reload_state.py') --pid $identity.pid --all-weapons --asset $row.name --seconds .2 --interval-ms 50 --output $inventoryPath 1> (Join-Path $Output 'preflight-stdout.txt') 2> (Join-Path $Output 'preflight-stderr.txt')
  if($LASTEXITCODE -ne 0){throw 'Read-only combined-episode preflight failed before injection'}
  $inventory=Load $inventoryPath
  if($inventory.schema -cne 'fvr.bc2.reload_state' -or -not $inventory.read_only -or $inventory.native_calls -ne $false -or $inventory.process_writes -ne $false -or $inventory.pid -ne $identity.pid -or $inventory.rejected.Count -or -not $inventory.samples.Count){throw 'Missing coherent read-only inventory evidence'}
  foreach($sample in $inventory.samples){
   if(-not $sample.identity_coherent -or $sample.asset_name -cne $row.name){throw 'Selected weapon changed before emulation'}
   $paired=@($sample.all_weapon_states|Where-Object {$_.asset_name -ceq 'SPAS12_sp'})
   if($paired.Count -ne 1 -or $paired[0].selected){throw 'The existing combined recipe requires one unselected SPAS; no loadout changes were made'}
   $states=@($paired[0].states.Values)
   if($states.Count -ne 2){throw 'Both observed shotgun copies are required'}
   foreach($state in $states){if($state.counter_7c -ne 8 -or $state.counter_80 -lt 2 -or $state.state_3c -ne 2){throw 'Paired SPAS must be full, finite and idle before the combined recipe'}}
  }
  }
  $game=Game;Idle
  Save 'emulation-sources.json' @{runner=(Pin $baseScript);build=(Pin $DiagnosticBuildReceipt);geometry=(Pin $GeometryCache);episode=$EmulationEpisode}
  $episodeSwitch=if($EmulationEpisode -eq 'SingleMagazine'){'-CampaignMagazine'}else{'-ManualPump'}
  & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $baseScript -ExpectedPid $identity.pid -ExpectedStartUtc $game.StartTime.ToUniversalTime().ToString('o') -GameDirectory (Split-Path $gameExe) -BuildReceipt $DiagnosticBuildReceipt -Output (Join-Path $Output 'emulation') -Python $Python -GeometryCache $GeometryCache -GeometrySha256 $GeometrySha256 $episodeSwitch -StartHeld -ExpectedAsset $row.name -AllowNativeImages -CaptureWidth $CaptureWidth -CaptureHeight $CaptureHeight 1> (Join-Path $Output 'stdout.txt') 2> (Join-Path $Output 'stderr.txt')
  $exitCode=$LASTEXITCODE
  $result=Load (Join-Path $Output 'emulation/completion.json');$images=Load (Join-Path $Output 'emulation/receiver/image-writes.json')
  if($exitCode -ne 0 -or -not $result.runtime_completed -or -not $result.responding -or $result.game_exited -or -not $images.files_disabled -or $images.queued -ne 0 -or $images.written -ne 0){throw 'Emulated episode failed; preserve evidence and do not retry automatically'}
 }else{
  $nativeArguments=@([string]$identity.pid,[string]$identity.creation_filetime,$Mode.ToLowerInvariant(),$binding.dll.path,(Join-Path $Output 'native.json'),[string]$asset,[string]$data,[string]$variant.variant,$gameExe)
  & $binding.runner.path @nativeArguments 1> (Join-Path $Output 'bootstrap.json') 2> (Join-Path $Output 'stderr.txt');$exitCode=$LASTEXITCODE
  $n=Load (Join-Path $Output 'native.json');$b=Load (Join-Path $Output 'bootstrap.json')
  $wanted=@{Observe=@(0,0,0);Equip=@(1,1,3);Restore=@(2,2,6)}[$Mode]
  if($exitCode -ne 0 -or $b.exit -ne 0 -or $b.pid -ne $identity.pid -or -not $b.dll_retained_for_restore -or $n.schema -cne 'fvr.bc2.provisioning_harness.v2' -or $n.pid -ne $identity.pid -or [string]$n.process_creation -cne [string]$identity.creation_filetime -or -not $n.complete -or -not $n.hooks_removed -or -not $n.code_restored -or $n.mode -ne $wanted[0] -or $n.native_calls -ne $wanted[1] -or $n.phase -ne $wanted[2]){throw 'Native provisioning did not complete and drain exactly'}
  if($n.target_variant -ne $variant.variant -or $n.target_name -cne $row.name -or ($n.target_path+'.dbx') -ine $row.resource -or $n.descriptor_profile_id -cne $variant.descriptor_profile_id -or $n.native_profile_id -ne $variant.native_profile_id){throw 'Completed native target differs from exact compiled selector'}
  $expectedAsset=if($priorNative){$priorNative.target_asset}else{$asset};$expectedData=if($priorNative){$priorNative.target_data}else{$data}
  if($n.target_asset -ne $expectedAsset -or $n.target_data -ne $expectedData){throw 'Native target address/lifetime changed'}
  if($Mode -ne 'Observe' -and (-not $n.server_item_observed -or -not $n.client_server_converged)){throw 'Native client/server convergence missing'}
  Save 'job.json' @{schema='fvr.bc2.campaign_bench.provision-job.v1';complete=$true;mode=$Mode;pid=$identity.pid;creation_filetime=$identity.creation_filetime;definition=$DefinitionId;plan_sha256=$PlanSha256;native_report=(Pin (Join-Path $Output 'native.json'));bootstrap_report=(Pin (Join-Path $Output 'bootstrap.json'));ammo_rollback_claimed=$false;original_item_pointer_restored=$false;native_reload_tested=$false}
 }
 $null=Game;$completed=$true
}catch{$failure=$_.Exception.Message;throw}
finally{
 Save 'completion.json' @{schema='fvr.bc2.campaign_bench.completion.v1';complete=$completed;error=$failure;mode=$Mode;definition=$DefinitionId;plan_sha256=$PlanSha256;pid=$identity.pid;creation_filetime=$identity.creation_filetime;diagnostic_build_sha256=$DiagnosticBuildReceiptSha256;exit_code=$exitCode;native_reload_accepted=$false;independent_receipt_audit_required=($Mode -eq 'Emulate');native_operation_tested=($completed -and $Mode -in @('Equip','Restore'));headset_verified=$false;automatic_restore=$false;automatic_retry=$false;game_killed=$false;raw_images=($rawBytes -gt 0);raw_capture_budget=$rawBytes;episode=$EmulationEpisode}
}

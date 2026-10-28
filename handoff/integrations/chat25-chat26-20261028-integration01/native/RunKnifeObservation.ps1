#requires -Version 7.5
param(
 [Parameter(Mandatory)][ValidateSet('Neutral','Motion')][string]$Mode,
 [switch]$SingleJab,
 [Parameter(Mandatory)][string]$BuildReceipt,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$BuildReceiptSha256,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$SourceSha256,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$BuiltSourceCommit,
 [Parameter(Mandatory)][string]$ProcessReceipt,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ProcessReceiptSha256,
 [Parameter(Mandatory)][string]$Output,
 [Parameter(Mandatory)][switch]$AllowBoundedImages,
 [string]$PriorCleanupReceipt,
 [string]$DirectionReceipt,
 [string]$DirectionReceiptSha256,
 [string]$RotationReceipt,
 [string]$RotationReceiptSha256,
 [switch]$ApproachCrouch,
 [switch]$ApproachResume,
 [switch]$ApproachPrepared,
 [ValidateRange(-6,6)][int]$ApproachTurn=0,
 [ValidateRange(0,10000)][int]$ApproachMs=0,
 [double]$ApproachStrafe=0,
 [double]$ApproachForward=0.5
)
# Private candidate session only. No game launch, restart, focus, OS input,
# process termination, installed-mod update, damage write or target teleport.
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo='G:\Unity\VRMMO\FrostbiteVR';$base=$PSScriptRoot
$runRoot=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01'
$nativeRoot=Join-Path $runRoot 'native'
$python='G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'
$gameExe='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
function LoadJson([string]$path){Get-Content -LiteralPath $path -Raw|ConvertFrom-Json}
function Sha([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function SaveJson([string]$name,$value){[IO.File]::WriteAllText((Join-Path $Output $name),($value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
function Below([string]$path,[string]$root){[IO.Path]::GetFullPath($path).StartsWith([IO.Path]::GetFullPath($root).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)}
function AssertPin([string]$path,[string]$hash){if($hash -notmatch '^[0-9a-f]{64}$' -or (Sha $path) -cne $hash){throw "Pinned file differs: $path"}}
function AssertLease {
 $lease=LoadJson 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
 if($lease.owner_token -cne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'This integration does not own the native session lease'}
}
AssertLease
. (Join-Path $PSScriptRoot 'Dependencies.ps1')
Assert-KnifeIntegrationDependencies
. (Join-Path $PSScriptRoot 'AssertKnifeCleanup427.ps1')
if(-not $AllowBoundedImages){throw 'Opt in to eight bounded native diagnostic images only; receiver review captures are disabled'}
$Output=[IO.Path]::GetFullPath($Output)
if(-not (Below $Output $nativeRoot) -or (Test-Path -LiteralPath $Output)){throw 'Fresh output directory below this integration native report root required'}
if($SingleJab -and ($Mode -ne 'Motion' -or $ApproachMs)){throw 'Single jab requires Motion without locomotion approach'}
$BuildReceipt=[IO.Path]::GetFullPath($BuildReceipt)
if($BuildReceipt -cne [IO.Path]::GetFullPath((Join-Path $runRoot 'normal-runtime/build-receipt.json'))){throw 'Exact frozen integration runtime required'}
if($ApproachTurn -and -not $ApproachMs){throw 'Turn requires a bounded approach'}
if($ApproachResume -and (-not $ApproachMs -or $ApproachTurn -or $ApproachPrepared)){throw 'Bounded resume requires straight approach without turns'}
if($ApproachPrepared -and ($Mode -ne 'Motion' -or -not $RotationReceipt -or -not $ApproachMs -or $ApproachTurn -or $ApproachResume)){throw 'Prepared approach requires measured motion with no turn/resume'}
AssertPin $BuildReceipt $BuildReceiptSha256
$build=LoadJson $BuildReceipt
if($build.normal -isnot [bool] -or -not $build.normal -or $build.source_sha256 -cne $SourceSha256 -or
   $build.built_source_commit -cne $BuiltSourceCommit -or $build.version -cne 'unreleased-chat25-chat26-20261028-integration01'){throw 'Wrong normal source/build receipt'}
$sourceReceipt=[IO.Path]::GetFullPath([string]$build.source_receipt)
if(-not (Below $sourceReceipt $runRoot)){throw 'Source receipt must belong to this integration'}
$source=LoadJson $sourceReceipt
if($source.schema -ne 'fvr.bc2.native-operation-source.v1' -or $source.source_sha256 -cne $SourceSha256){throw 'Native source receipt differs from frozen build'}
$sourceReceiptHash=Sha $sourceReceipt
AssertPin $sourceReceipt $build.source_receipt_sha256
if($build.schema -cne 'fvr.checkpoint436.normal-build.v1' -or $build.full_normal_builds -isnot [bool] -or
   -not $build.full_normal_builds -or $build.diagnostic_only -isnot [bool] -or $build.diagnostic_only){throw 'Paired full normal build required'}
AssertPin $build.source_freeze.path $build.source_freeze.sha256
AssertPin $build.reviewed_header.path $build.reviewed_header.sha256
if(@($build.validation).Count -ne 2){throw 'Paired validation receipts missing'}
foreach($architecture in 'x86','x64'){
 $entries=@($build.validation|Where-Object {$_.architecture -ceq $architecture})
 if($entries.Count -ne 1){throw 'Unique paired architecture validation required'}
 $entry=$entries[0]
 foreach($pin in $entry.evidence.PSObject.Properties.Value){AssertPin $pin.path $pin.sha256}
 $validation=LoadJson $entry.evidence.'build.json'.path
 $result=LoadJson $entry.evidence.'result.json'.path
 if($validation.built -isnot [bool] -or -not $validation.built -or $validation.tests_passed -isnot [bool] -or
    -not $validation.tests_passed -or $validation.architecture -cne $architecture -or
    $result.passed -isnot [bool] -or -not $result.passed -or $result.source_commit -cne $BuiltSourceCommit){throw 'Full normal validation failed'}
}
foreach($name in 'probe','trace','receiver','host'){
 $binary=$build.binaries.$name
 if(-not (Below $binary.path (Split-Path $BuildReceipt))){throw 'Binary outside frozen normal runtime'}
 AssertPin $binary.path $binary.sha256
}
AssertPin $gameExe '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
if(-not ((Below $ProcessReceipt 'G:\Unity\VRMMO\bc2vr-recovery\performance436') -or (Below $ProcessReceipt $nativeRoot))){throw 'Current root process receipt required'}
AssertPin $ProcessReceipt $ProcessReceiptSha256
$identity=LoadJson $ProcessReceipt
if($identity.observation_schema -cne 'fvr.bc2.knife-observation-process.v1'){throw 'Game was not launched through the observed environment route'}
AssertPin $identity.original_process.path $identity.original_process.sha256
AssertPin $identity.observer_launcher.path $identity.observer_launcher.sha256
AssertPin $identity.profile_backup.path $identity.profile_backup.sha256
if($identity.observer_launcher.path -cne (Join-Path $PSScriptRoot 'StartObservedGame.ps1')){throw 'Exact integration launch wrapper required'}
$originalIdentity=LoadJson $identity.original_process.path
if($identity.pid -ne $originalIdentity.pid -or $identity.creation_filetime -cne $originalIdentity.creation_filetime -or $identity.path -cne $originalIdentity.path){throw 'Observation process receipt does not match original launch'}
foreach($flag in 'FVR_BC2_PRIVATE_KNIFE_SOUND_OBSERVE','FVR_BC2_PRIVATE_KNIFE_TIMING_OBSERVE'){
 if($identity.observer_environment.$flag -cne '1'){throw 'Required observation was not set in the game launch environment'}
}
Assert-KnifeProcessIdentity427 $identity $gameExe
function AssertGame {
 AssertLease
 $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
 if($games.Count -ne 1 -or $games[0].Id -ne $identity.pid){throw 'Exactly the admitted game must be running'}
 $p=$games[0]
 if($p.Path -ne $gameExe -or $p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne [string]$identity.creation_filetime){throw 'Game lifetime changed'}
 return $p
}
function AssertIdle {
 if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,SceneMenuSidecar421,SceneMenuSidecar431 -ErrorAction SilentlyContinue){throw 'Another mod/session helper is active'}
}
$game=AssertGame;AssertIdle
foreach($name in 'BC2_WEATHER_OBSERVER_DELAY_MS','BC2_WEATHER_TRANSLATION_EXPERIMENT','BC2_WEATHER_TRANSLATION','BC2_WEATHER_OBSERVER'){
 if([Environment]::GetEnvironmentVariable($name,'Process') -or $identity.observer_environment.$name){throw 'Unbudgeted weather capture environment'}
}
# Every retained module needs exact prior clean-stop evidence, including its
# process lifetime and original trace exit. File hashes alone are insufficient.
$prior=@()
if($PriorCleanupReceipt){
 $cleanup=LoadJson $PriorCleanupReceipt
 if($cleanup.schema -cne 'fvr.retained-probe-cleanup426.v1' -or -not(Test-KnifeUnsigned427 $cleanup.pid) -or $cleanup.pid -ne $identity.pid -or $cleanup.creation_filetime -isnot [string] -or $cleanup.creation_filetime -cne $identity.creation_filetime -or $cleanup.modules -isnot [array]){throw 'Prior cleanup lifetime differs'}
 $prior=@($cleanup.modules)
}
$retainedModules=@($game.Modules|Where-Object {$_.ModuleName -like 'BC2NativeProbe*'})
if($prior.Count -ne $retainedModules.Count){throw 'Cleanup receipt does not exactly cover the retained probe set'}
foreach($module in $retainedModules){
 $matches=@($prior|Where-Object {$_.path -eq $module.FileName})
 if($matches.Count -ne 1){throw "Retained probe lacks one exact cleanup receipt: $($module.FileName)"}
 $entry=$matches[0];AssertPin $entry.path $entry.sha256
 AssertPin $entry.native_report $entry.native_report_sha256;AssertPin $entry.trace_stdout $entry.trace_stdout_sha256
 $native=LoadJson $entry.native_report;$bootstrap=LoadJson $entry.trace_stdout
 Assert-KnifeNativeCleanup427 $native $bootstrap $identity.pid $entry.trace_exit (Read-KnifeCleanupProgress427 $entry.native_report)
}
$direction=$null;$rotation=$null;$motionAxis=$null
if($Mode -eq 'Motion'){
 AssertPin $DirectionReceipt $DirectionReceiptSha256;$direction=LoadJson $DirectionReceipt
 if($direction.schema -ne 'fvr.selected_knife_receiver_direction426.v1' -or $direction.offline -ne $true -or @($direction.axis_xr).Count -ne 3){throw 'Measured LOCAL direction receipt required'}
 foreach($entry in $direction.files){AssertPin $entry.path $entry.sha256}
 $norm=0.0;foreach($v in $direction.axis_xr){if([double]::IsNaN($v) -or [double]::IsInfinity($v)){throw 'Nonfinite direction'};$norm+=[double]$v*[double]$v}
 if([Math]::Abs($norm-1.0) -gt .00001){throw 'Direction must be normalized'}
 $motionAxis=$direction.axis_xr
}elseif($DirectionReceipt -or $DirectionReceiptSha256){throw 'Only Motion uses a direction receipt'}
if($RotationReceipt){
 if($Mode -ne 'Motion'){throw 'Measured wrist rotation requires Motion'}
 AssertPin $RotationReceipt $RotationReceiptSha256;$rotation=LoadJson $RotationReceipt
 if($rotation.schema -ne 'fvr.selected_knife_forward_proposal427.v1' -or -not $rotation.offline -or $rotation.native_authority -or $rotation.source_direction_pid -ne $identity.pid -or $rotation.source_direction_weapon -ne $direction.weapon -or $rotation.source_direction_space -ne $direction.space){throw 'Rotation proposal belongs to a different measured lifetime'}
 foreach($entry in $rotation.files){AssertPin $entry.path $entry.sha256}
 $directionPins=@($rotation.files|Where-Object {$_.path -eq $DirectionReceipt -and $_.sha256 -ceq $DirectionReceiptSha256})
 if($directionPins.Count -ne 1){throw 'Rotation proposal does not pin the admitted direction'}
 $q=@($rotation.right_grip_quaternion_xyzw);$axis=@($rotation.requested_axis_xr)
 if($q.Count -ne 4 -or $axis.Count -ne 3){throw 'Malformed rotation proposal'}
 foreach($v in @($q)+@($axis)){if([double]::IsNaN($v) -or [double]::IsInfinity($v)){throw 'Nonfinite rotation proposal'}}
 $norm=0.0;foreach($v in $q){$norm+=[double]$v*[double]$v}
 if([Math]::Abs($norm-1) -gt .00001 -or $q[3] -lt [Math]::Sqrt(.5) -or $q[3] -gt 1 -or $axis[0] -ne 0 -or $axis[1] -ne 0 -or $axis[2] -ne -1){throw 'Rotation must be a bounded unit quaternion toward LOCAL forward'}
 $a=@($direction.axis_xr);$cross=@(($q[1]*$a[2]-$q[2]*$a[1]),($q[2]*$a[0]-$q[0]*$a[2]),($q[0]*$a[1]-$q[1]*$a[0]))
 $doubleCross=@(($q[1]*$cross[2]-$q[2]*$cross[1]),($q[2]*$cross[0]-$q[0]*$cross[2]),($q[0]*$cross[1]-$q[1]*$cross[0]))
 for($i=0;$i -lt 3;$i++){if([Math]::Abs($a[$i]+2*$q[3]*$cross[$i]+2*$doubleCross[$i]-$axis[$i]) -gt .00001){throw 'Rotation does not align the measured blade'}}
 $motionAxis=$axis
}elseif($RotationReceiptSha256){throw 'Rotation hash without receipt'}
if($ApproachCrouch -and -not $ApproachMs){throw 'Crouch requires the bounded approach'}
. (Join-Path $repo 'tools/StorageBudget.ps1');$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$env:PYTHONDONTWRITEBYTECODE='1'
# Both modes disable receiver image files; the native probe keeps eight captures.
$rawFrames=8
if($ApproachMs){
 if($Mode -eq 'Neutral' -or [double]::IsNaN($ApproachStrafe) -or [double]::IsNaN($ApproachForward) -or $ApproachForward -lt 0 -or ($ApproachStrafe*$ApproachStrafe+$ApproachForward*$ApproachForward) -gt .25 -or ($ApproachStrafe -eq 0 -and $ApproachForward -eq 0)){throw 'Invalid bounded approach'}
 $rawFrames=8
}
# Native CaptureWorldColor refuses either axis above4096; reserve its hard maximum.
# Both admitted paths use --no-image-files; pose and input journals remain enabled.
if($rawFrames -ne 8){throw 'This observation admits only eight native captures'}
$rawBytes=[long]$rawFrames*4096*4096*4
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees') -OutputPath $Output -Operation Session -RawCaptureBytes $rawBytes -PlannedBytes ($rawBytes+128MB)
$null=New-Item -ItemType Directory -Path $Output
SaveJson 'storage.json' $budget
SaveJson 'admission.json' @{mode=$Mode;single_jab=[bool]$SingleJab;built_source_commit=$BuiltSourceCommit;process_receipt=$ProcessReceipt;process_receipt_sha256=(Sha $ProcessReceipt);pid=$identity.pid;creation_filetime=$identity.creation_filetime;build_receipt=$BuildReceipt;build_receipt_sha256=$BuildReceiptSha256;source_sha256=$SourceSha256;source_receipt=$sourceReceipt;source_receipt_sha256=$sourceReceiptHash;prior_cleanup=$PriorCleanupReceipt;direction_receipt=$DirectionReceipt;direction_receipt_sha256=$DirectionReceiptSha256;rotation_receipt=$RotationReceipt;rotation_receipt_sha256=$RotationReceiptSha256;approach_crouch=[bool]$ApproachCrouch;approach_prepared=[bool]$ApproachPrepared;os_input=$false;headset_verified=$false}
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $Output 'runner.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'AssertKnifeCleanup427.ps1') -Destination (Join-Path $Output 'AssertKnifeCleanup427.ps1')
SaveJson 'navigation.json' @{approach_ms=$ApproachMs;strafe=$ApproachStrafe;forward=$ApproachForward;crouch=[bool]$ApproachCrouch;turn_steps=$ApproachTurn;turn_prefix_ms=([Math]::Abs($ApproachTurn)*500);prepared_motion=[bool]$ApproachPrepared;resume_one_expired_movement_lease=[bool]$ApproachResume;resume_initial_neutral_lease=[bool]$ApproachResume;resume_pause_budget_shared=$true;maximum_resume_pause_ms=$(if($ApproachResume){2500}else{0});receiver_review_pairs=0;raw_frame_budget=$rawFrames;raw_byte_budget=$rawBytes;os_input=$false}
& $python -B (Join-Path $base 'ReadInventory430.py') --selected-knife-server --process $ProcessReceipt --output (Join-Path $Output 'inventory-before.json') *> (Join-Path $Output 'inventory-before.log')
if($LASTEXITCODE){throw 'Native inventory preflight failed; no input/injection started'}
$inventory=LoadJson (Join-Path $Output 'inventory-before.json');$owner=$inventory.owner
$selected=@($inventory.items|Where-Object {$_.slot -eq 0 -and $_.weapon -eq $owner.selected_weapon})
if($owner.selected_slot -ne 0 -or $selected.Count -ne 1){throw 'Current selected opening knife must be slot0'}
$weapon=$selected[0];$config=$weapon.configuration
if($weapon.name -ne 'KNV-1' -or $weapon.path -cne 'Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable' -or $config.fire_logic.FireInputAction.value -ne 37 -or $config.fire_logic.FireLogicType.value -ne 0 -or $config.ammo_config.MagazineCapacity -ne -1 -or $config.ammo_config.NumberOfMagazines -ne -1){throw 'Current weapon is not authored selected Knife_Equipable/action37'}
if($direction){
 if($direction.weapon -ne $weapon.weapon){throw 'Measured direction belongs to a different selected weapon'}
 $directionNative=LoadJson $direction.files[0].path
 $origins=@($prior|Where-Object {$_.native_report -eq $direction.files[0].path -and $_.native_report_sha256 -ceq $direction.files[0].sha256})
 if($directionNative.pid -ne $identity.pid -or -not $directionNative.hooks_disabled -or $origins.Count -ne 1){throw 'Direction source is not a completed trace bound to this exact process lifetime'}
}
# A unique same-hash filename is necessary: a retained probe has a one-shot
# static entry latch. Never overwrite an existing loaded DLL or installed425.
$probe=Join-Path $Output ('BC2NativeProbeKnifeObservation-'+$Mode.ToLowerInvariant()+'.dll')
Copy-Item -LiteralPath $build.binaries.probe.path -Destination $probe
AssertPin $probe $build.binaries.probe.sha256
function StartHidden([string]$name,[string]$exe,[string[]]$arguments){
 foreach($argument in $arguments){if($argument.Contains('"')){throw 'Unsupported quote in child argument'}}
 SaveJson ($name+'-arguments.json') (@($exe)+$arguments)
 $quoted=@($arguments|ForEach-Object {'"'+$_+'"'})
 Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $Output -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Output ($name+'-stdout.json')) -RedirectStandardError (Join-Path $Output ($name+'-stderr.txt'))
}
$receiverDir=Join-Path $Output 'receiver';$receiverArgs=@('--output',$receiverDir,'--static-pose','--async')
$seconds=0
switch($Mode){
 Neutral {$receiverArgs+=@('--selected-knife-neutral','--pairs','240','--no-image-files');$seconds=60}
 Motion {$receiverArgs+=@('--selected-knife-motion-smoke');$receiverArgs+=@($motionAxis|ForEach-Object {([double]$_).ToString('G9',[Globalization.CultureInfo]::InvariantCulture)});$receiverArgs+=@('--pairs','240','--no-image-files');$seconds=11}
}
if($SingleJab){$receiverArgs+='--selected-knife-single-jab'}
if($ApproachMs){
 $receiverArgs=@('--output',$receiverDir,'--static-pose','--async','--pairs','320','--no-image-files','--selected-knife-approach',[string]$ApproachMs,$ApproachStrafe.ToString('G9',[Globalization.CultureInfo]::InvariantCulture),$ApproachForward.ToString('G9',[Globalization.CultureInfo]::InvariantCulture))
 $receiverArgs+='--selected-knife-motion-smoke';$receiverArgs+=@($motionAxis|ForEach-Object {([double]$_).ToString('G9',[Globalization.CultureInfo]::InvariantCulture)});$seconds=[int][Math]::Ceiling(($ApproachMs+9000)/1000.0)+3
}
if($ApproachCrouch){$receiverArgs+='--selected-knife-approach-crouch'}
if($ApproachResume){$receiverArgs+='--selected-knife-approach-resume';$seconds+=3}
if($ApproachTurn){$receiverArgs+=@('--selected-knife-approach-turn',[string]$ApproachTurn);$seconds+=[int][Math]::Ceiling([Math]::Abs($ApproachTurn)*.5)}
if($rotation){$receiverArgs+='--selected-knife-motion-rotation';$receiverArgs+=@($rotation.right_grip_quaternion_xyzw|ForEach-Object {([double]$_).ToString('G9',[Globalization.CultureInfo]::InvariantCulture)})}
if($ApproachPrepared){$receiverArgs+='--selected-knife-approach-prepared'}
$receiver=$null;$trace=$null;$failure=$null
try {
 $game=AssertGame;AssertIdle
 $receiver=StartHidden 'receiver' $build.binaries.receiver.path $receiverArgs
 $channelPath=Join-Path $receiverDir 'channel.txt'
 $readyUntil=[DateTime]::UtcNow.AddSeconds(5)
 while(-not (Test-Path -LiteralPath $channelPath)){
  if($receiver.HasExited -or [DateTime]::UtcNow -gt $readyUntil){throw 'Receiver channel unavailable within five seconds'}
  Start-Sleep -Milliseconds 25
 }
 $channel=(Get-Content -LiteralPath $channelPath -Raw).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid receiver channel'}
 $game=AssertGame
 $traceArgs=@('--game',(Split-Path $gameExe),'--dll',$probe,'--report',(Join-Path $Output 'native-trace.json'),'--stream',$channel,'--seconds',[string]$seconds,'--body-inventory','--magazine-reload','--sight-flip','--boat-head-aim','--boat-head-fire')
 $trace=StartHidden 'trace' $build.binaries.trace.path $traceArgs
 $deadline=[DateTime]::UtcNow.AddSeconds($seconds+35)
 while(-not $trace.WaitForExit(500)){
  if([DateTime]::UtcNow -gt $deadline){throw 'Trace exceeded bound; leave game and native thread intact for root inspection'}
 }
 $receiverDone=$receiver.WaitForExit(12000)
 if($trace.ExitCode -ne 0 -or -not $receiverDone){throw 'Trace/receiver did not finish within the cleanup bound; inspect retained logs'}
 $game=AssertGame
 $native=LoadJson (Join-Path $Output 'native-trace.json');$boot=LoadJson (Join-Path $Output 'trace-stdout.json')
 Assert-KnifeNativeCleanup427 $native $boot $identity.pid $trace.ExitCode (Read-KnifeCleanupProgress427 (Join-Path $Output 'native-trace.json'))
 # Native retirement and diagnostic success are distinct. Preserve verified
 # cleanup even if the receiver cancelled, without relabeling the failed test.
 $entry=@{path=$probe;sha256=(Sha $probe);native_report=(Join-Path $Output 'native-trace.json');native_report_sha256=(Sha (Join-Path $Output 'native-trace.json'));trace_stdout=(Join-Path $Output 'trace-stdout.json');trace_stdout_sha256=(Sha (Join-Path $Output 'trace-stdout.json'));trace_exit=$trace.ExitCode}
 SaveJson 'retained-probe-cleanup.json' @{schema='fvr.retained-probe-cleanup426.v1';pid=$identity.pid;creation_filetime=$identity.creation_filetime;modules=@($prior)+@($entry);cleanup_only=$true;receiver_exit=$receiver.ExitCode;receiver_test_failed=($receiver.ExitCode -ne 0);cleanup_helper_sha256=(Sha (Join-Path $Output 'AssertKnifeCleanup427.ps1'))}
 if($receiver.ExitCode -ne 0){throw 'Receiver diagnostic failed; exact native cleanup is preserved separately'}
 & $python -B (Join-Path $base 'ReadInventory430.py') --selected-knife-server --process $ProcessReceipt --output (Join-Path $Output 'inventory-after.json') *> (Join-Path $Output 'inventory-after.log')
 $postflight=$LASTEXITCODE
 $knife=$native.gameplay.selected_knife_native
 if(-not $knife.sound_observation.enabled -or -not $knife.timing_observation.enabled -or
    -not $native.gameplay.reload_flow.knife_export_observation_hook -or
    $knife.sound_observation.sounds_suppressed -ne 0 -or $knife.timing_observation.native_timing_modified){
  throw 'Observation not enabled or changed native audio/timing; cleanup remains independently preserved'
 }
 $imageWrites=LoadJson (Join-Path $receiverDir 'image-writes.json')
 if($imageWrites.files_disabled -isnot [bool] -or -not $imageWrites.files_disabled -or $imageWrites.written -ne 0){throw 'Receiver image opt-out was not honored'}
 if($Mode -eq 'Motion'){
  $journal=Get-Item -LiteralPath (Join-Path $receiverDir 'trigger-input.jsonl')
  if($journal.Length -le 0 -or $journal.Length -gt 16MB){throw 'Bounded motion journal missing or oversized'}
  $stopped=Get-Content -LiteralPath $journal.FullName -Tail 1|ConvertFrom-Json
  if($stopped.event -cne 'stopped' -or $stopped.journal_error -isnot [bool] -or $stopped.journal_error){throw 'Motion journal did not finish'}
  $motion=$stopped.summary
  if($motion.schema -ceq 'fvr.selected_knife_approach_smoke.v1'){$motion=$motion.attack}
  $expectedStrokes=if($SingleJab){1}else{2}
  if($motion.schema -cne 'fvr.selected_knife_motion_smoke.v1' -or $motion.stroke_count -ne $expectedStrokes -or
     $motion.publication_complete -isnot [bool] -or -not $motion.publication_complete){throw 'Expected bounded motion publication did not complete'}
 }
 $completed=@($knife.records|Where-Object {$_.attack_returned -and $_.acquisition_returned -and $_.failure -eq 0})
 $strokes=@($completed|Where-Object {$_.cause -eq 1 -and $_.copies -gt 0}|Sort-Object serial -Unique)
 $misses=@($strokes|Where-Object {$_.target -eq 0 -and $_.weak_handle -eq 0})
 $damaged=@($completed|Where-Object {$_.damage_calls -eq 1 -and $_.health_known -and $_.health_after -lt $_.health_before})
 $nativeDamage=@($completed|Where-Object {$_.damage_calls -gt 0 -and $_.target -ne 0 -and $_.weak_handle -ne 0})
 $damageEvidence=@($nativeDamage|Select-Object serial,input,cause,target,weak_handle,damage_calls,health_known,health_before,health_after)
 $missThenAgain=$false;foreach($miss in $misses){if(@($strokes|Where-Object {$_.serial -gt $miss.serial -and $_.acquired_ns -gt $miss.attacked_ns}).Count){$missThenAgain=$true}}
 SaveJson 'evidence-summary.json' @{mode=$Mode;requested_stroke_count=$(if($Mode -eq 'Neutral'){0}elseif($SingleJab){1}else{2});native_adapter_verified=[bool]$knife.verified;prepared=$knife.prepared;pending=$knife.pending;completed_stroke_serials=@($strokes|ForEach-Object {$_.serial});completed_miss_serials=@($misses|ForEach-Object {$_.serial});native_damage_callbacks=$damageEvidence;health_decrease_serials=@($damaged|ForEach-Object {$_.serial});two_strokes_completed=($strokes.Count -ge 2);miss_then_later_stroke_completed=$missThenAgain;input_delivery_is_not_native_submission=$true;scripted_guard_response_verified=$false;campaign_progress_verified=$false;headset_verified=$false;postflight_exit=$postflight;full_records='native-trace.json:gameplay.selected_knife_native.records'}
 if($postflight -ne 0){throw 'Native inventory postflight failed; original failure and exact cleanup are retained'}
}catch{$failure=$_.Exception.Message;throw}
finally {
 # Natural helper deadlines are retained even on failure. Never terminate a
 # process to fabricate cleanup or native completion.
 SaveJson 'completion.json' @{pid=$identity.pid;creation_filetime=$identity.creation_filetime;mode=$Mode;error=$failure;trace_pid=$(if($trace){$trace.Id}else{$null});trace_exited=$(if($trace){$trace.HasExited}else{$false});trace_exit=$(if($trace -and $trace.HasExited){$trace.ExitCode}else{$null});receiver_pid=$(if($receiver){$receiver.Id}else{$null});receiver_exited=$(if($receiver){$receiver.HasExited}else{$false});receiver_exit=$(if($receiver -and $receiver.HasExited){$receiver.ExitCode}else{$null});os_input=$false;focus_activation=$false;game_process_stopped=$false;installed_files_changed=$false;headset_verified=$false}
}

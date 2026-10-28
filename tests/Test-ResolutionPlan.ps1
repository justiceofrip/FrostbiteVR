#requires -Version 7.5
# Pure policy fixtures: no game, headset, settings or filesystem mutations.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../launcher/runtime/Resolution.ps1')
$script:checks=0
function Check([bool]$Okay,[string]$Label){if(-not $Okay){throw $Label};++$script:checks}
function Reject([scriptblock]$Action,[string]$Label){$failed=$false;try{& $Action | Out-Null}catch{$failed=$true};Check $failed $Label}
function Probe {return @{instance_created=$true;system_available=$true;recommended_eye_width=2784;recommended_eye_height=3024;max_eye_width=8192;max_eye_height=8192;eye_width=2784;eye_height=3024}}
$probe=Probe;$plan=Get-Bc2ResolutionPlan $probe
Check ($plan.width -eq 2784 -and $plan.height -eq 3024 -and -not $plan.native_source_verified) 'Runtime size is planned, not falsely marked as rendered.'
foreach($key in @('recommended_eye_width','recommended_eye_height','max_eye_width','max_eye_height','eye_width','eye_height')) {
    $p=Probe;$p.Remove($key);Reject {Get-Bc2ResolutionPlan $p} "Missing $key rejected."
    foreach($value in @($false,'2784',2784.0,0,-1)) {$p=Probe;$p[$key]=$value;Reject {Get-Bc2ResolutionPlan $p} "Malformed $key rejected."}
}
$p=Probe;$p.max_eye_width=2700;Reject {Get-Bc2ResolutionPlan $p} 'Incompatible per-eye maximum rejected.'
$p=Probe;$p.eye_width=1920;Reject {Get-Bc2ResolutionPlan $p} 'Desktop override cannot pass as native recommendation.'
$p=Probe;$p.recommended_eye_width=$p.eye_width=8192;$p.recommended_eye_height=$p.eye_height=8192;Reject {Get-Bc2ResolutionPlan $p} 'Unbounded pixel allocation rejected.'
$p=Probe;$p.system_available=$false;Reject {Get-Bc2ResolutionPlan $p} 'Disconnected runtime cannot plan a launch.'
$lease=New-Bc2ResolutionLease @{pid=42;start_utc='2026-10-21T00:00:00Z'} @{pid=43;start_utc='2026-10-21T00:00:01Z'} ('a'*64)
$proof=$lease.Clone();$proof.schema='bc2vr.native-resolution.v2';$proof.operation='configure'
foreach($pair in @{
    passed=$true;applied=$true;hook_restored=$true;callbacks_drained=$true;requested_width=2784;requested_height=3024;width=2784;height=3024;updates=5;ui_thread=7;
    guard_installed=$true;guard_active=$true;guard_disabled=$false;guard_drained=$false;guard_released=$false;guard_owner_lost=$false;guard_cleanup_pending=$false;guard_never_installed=$false
}.GetEnumerator()){$proof[$pair.Key]=$pair.Value}
Assert-Bc2NativeResolution $plan $proof $lease;++$script:checks
foreach($key in @('passed','applied','hook_restored','callbacks_drained','guard_installed','guard_active')) {$bad=$proof.Clone();$bad[$key]=$false;Reject {Assert-Bc2NativeResolution $plan $bad $lease} "Unconfirmed $key rejected."}
foreach($key in @('guard_disabled','guard_drained','guard_released','guard_owner_lost','guard_cleanup_pending','guard_never_installed')){$bad=$proof.Clone();$bad[$key]=$true;Reject {Assert-Bc2NativeResolution $plan $bad $lease} "Invalid live guard $key rejected."}
foreach($key in @('requested_width','width','requested_height','height')) {$bad=$proof.Clone();$bad[$key]=1920;Reject {Assert-Bc2NativeResolution $plan $bad $lease} "Mismatched $key rejected."}
foreach($key in @('pid','supervisor_pid','creation_filetime','supervisor_creation_filetime','session_token')){
    $bad=$proof.Clone();$bad.Remove($key);Reject {Assert-Bc2NativeResolution $plan $bad $lease} "Missing lease $key rejected."
    $bad=$proof.Clone();$bad[$key]=$(if($key -in @('pid','supervisor_pid')){99}else{'stale'});Reject {Assert-Bc2NativeResolution $plan $bad $lease} "Wrong lease $key rejected."
}
$bad=$proof.Clone();$bad.updates=3;Reject {Assert-Bc2NativeResolution $plan $bad $lease} 'No stable post-apply renderer evidence rejected.'
$release=$proof.Clone();$release.operation='release';$release.guard_active=$false;$release.guard_released=$true;$release.guard_disabled=$true;$release.guard_drained=$true
Assert-Bc2ResolutionReleased $release $lease;++$script:checks
$never=$release.Clone();$never.guard_released=$false;$never.guard_never_installed=$true;$never.Remove('hook_restored');$never.Remove('callbacks_drained');Assert-Bc2ResolutionReleased $never $lease;++$script:checks
foreach($key in @('hook_restored','callbacks_drained')){
    $bad=$release.Clone();$bad[$key]=$false;Reject {Assert-Bc2ResolutionReleased $bad $lease} "Released guard with incomplete temporary cleanup $key rejected."
    $bad=$release.Clone();$bad.Remove($key);Reject {Assert-Bc2ResolutionReleased $bad $lease} "Released guard with missing temporary cleanup $key rejected."
}
foreach($key in @('guard_disabled','guard_drained','passed')){$bad=$release.Clone();$bad[$key]=$false;Reject {Assert-Bc2ResolutionReleased $bad $lease} "Release requires $key."}
foreach($key in @('guard_active','guard_cleanup_pending')){$bad=$release.Clone();$bad[$key]=$true;Reject {Assert-Bc2ResolutionReleased $bad $lease} "Incomplete release $key rejected."}
$bad=$release.Clone();$bad.guard_released=$false;Reject {Assert-Bc2ResolutionReleased $bad $lease} 'No release outcome rejected.'
$bad=$release.Clone();$bad.session_token='b'*32;Reject {Assert-Bc2ResolutionReleased $bad $lease} 'Another lease cannot confirm release.'
$bad=$release.Clone();$bad.operation='configure';Reject {Assert-Bc2ResolutionReleased $bad $lease} 'Setup receipt cannot confirm release.'
"Resolution planning: $script:checks checks passed (pure fixtures; no native acceptance)."

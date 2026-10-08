[CmdletBinding()]
param([Alias('MuzzleFireProbe')][switch]$MuzzleFire,[switch]$TwoHandGrip,[switch]$SightFlip,[switch]$ReloadHoldProbe,[switch]$PumpHoldProbe,[switch]$ReloadRoundProbe,[switch]$ReloadRequestProbe,[ValidateSet("None","Insert","Cancel")][string]$Xm8MagazineProbe="None",[switch]$MagazineReload,[switch]$MagazinePhysicalProbe,[switch]$MagazineOriginalReturnProbe,[switch]$MagazineFullReturnProbe,[switch]$BoatHeadAim,[switch]$BoatHeadFire,[switch]$PhysicalReload,[switch]$PhysicalReloadProbe,[switch]$PhysicalReloadRepeatProbe,[switch]$OpticFilterObserve,[switch]$BodyInventory,[switch]$WeaponVisibilityProbe,[switch]$BodyHolsterProbe,[switch]$BodyHolsterXm8Probe,[switch]$BodyHolsterXm8FireProbe,[switch]$EquipProbe,[switch]$DeathProbe,[switch]$Controllers,[switch]$MotionAim,[switch]$BodyFollow,[switch]$PoseObserve,[switch]$RigPulse,[switch]$Hands,[switch]$UntilHostExit,[ValidateRange(0,2147483647)][int]$HostPid=0,[switch]$PassEvidence,[switch]$UncapMirror,[switch]$CameraPulse,[switch]$VisibilityPulse,[switch]$ViewLifecycle,[switch]$OwnershipV2,[switch]$StereoOnce,[switch]$InitializeView,[switch]$WorkPool,[switch]$StereoBurst,[string]$FrameChannel,[ValidateRange(1,60)][int]$Seconds=3)
if(([int]$CameraPulse.IsPresent+[int]$VisibilityPulse.IsPresent+[int]$ViewLifecycle.IsPresent+[int]$OwnershipV2.IsPresent+[int]$StereoOnce.IsPresent+[int]$InitializeView.IsPresent+[int]$WorkPool.IsPresent+[int]$StereoBurst.IsPresent+[int](-not [string]::IsNullOrEmpty($FrameChannel))) -gt 1){throw 'Select only one diagnostic mode.'}
if($ViewLifecycle){throw 'View lifecycle experiment is quarantined after the 11:35:09 UTC exit. See docs/CRASH-20260925.md.'}
$ErrorActionPreference='Stop'
if($BodyHolsterXm8FireProbe){if($BodyHolsterXm8Probe -or $BodyHolsterProbe){throw 'Select one explicit holster diagnostic'};$BodyHolsterXm8Probe=$true}
if($BodyHolsterXm8Probe){$BodyHolsterProbe=$true}
if($BoatHeadFire -and -not $BoatHeadAim){throw 'BoatHeadFire requires explicit BoatHeadAim'}
if($BoatHeadAim){
    if(-not $FrameChannel -or $PassEvidence -or $MagazinePhysicalProbe -or $MagazineOriginalReturnProbe -or $MagazineFullReturnProbe -or $Xm8MagazineProbe -ne 'None' -or $PhysicalReloadProbe -or $PhysicalReloadRepeatProbe -or $ReloadRequestProbe -or $ReloadRoundProbe -or $ReloadHoldProbe -or $BodyHolsterProbe -or $WeaponVisibilityProbe -or $OpticFilterObserve -or $RigPulse -or $EquipProbe -or $DeathProbe){throw 'BoatHeadAim requires a normal tracked stream without synthetic diagnostics'}
    $Controllers=$true;$MotionAim=$true;$BodyFollow=$true
}
if(([int][bool]$MagazineReload+[int][bool]$MagazinePhysicalProbe+[int][bool]$MagazineOriginalReturnProbe+[int][bool]$MagazineFullReturnProbe+[int]($Xm8MagazineProbe -ne 'None')) -gt 1){throw 'Select one magazine mode'}
if($MagazineReload -or $MagazinePhysicalProbe -or $MagazineOriginalReturnProbe -or $MagazineFullReturnProbe){
    if(-not $FrameChannel -or $PassEvidence -or $PhysicalReloadProbe -or $PhysicalReloadRepeatProbe -or $ReloadRequestProbe -or $ReloadRoundProbe -or $ReloadHoldProbe -or $BodyHolsterProbe -or $WeaponVisibilityProbe -or $OpticFilterObserve -or $RigPulse -or $EquipProbe -or $DeathProbe){throw 'Magazine reload requires tracked physical input without diagnostic actions'}
    if(($MagazinePhysicalProbe -or $MagazineOriginalReturnProbe -or $MagazineFullReturnProbe) -and ($Seconds -ne 30 -or $UntilHostExit -or $HostPid -or $SightFlip -or $BodyInventory)){throw 'MagazinePhysicalProbe requires isolated30s neutral tracked stream'}
    $PhysicalReload=$true
}
if($Xm8MagazineProbe -ne 'None'){
    if($Seconds -ne 30 -or -not $FrameChannel -or $UntilHostExit -or $HostPid -or $PassEvidence -or $PhysicalReload -or $PhysicalReloadProbe -or $PhysicalReloadRepeatProbe -or $ReloadRequestProbe -or $ReloadRoundProbe -or $ReloadHoldProbe -or $BodyInventory -or $BodyHolsterProbe -or $WeaponVisibilityProbe -or $OpticFilterObserve -or $SightFlip -or $RigPulse -or $EquipProbe -or $DeathProbe){throw 'XM8 magazine probe requires isolated30s tracked stream'}
    $Controllers=$true;$MotionAim=$true;$BodyFollow=$true;$PoseObserve=$true;$Hands=$true;$MuzzleFire=$true;$TwoHandGrip=$true
}
if($WeaponVisibilityProbe -and $BodyHolsterProbe){throw 'Select one visibility/body diagnostic'}
if($WeaponVisibilityProbe){
    if($UntilHostExit -or $HostPid -ne 0 -or $Seconds -ne 15 -or -not $FrameChannel -or $BodyInventory -or $OpticFilterObserve -or $PhysicalReload -or $PhysicalReloadRepeatProbe -or $PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadRoundProbe -or $ReloadHoldProbe -or $SightFlip -or $RigPulse -or $DeathProbe -or $EquipProbe -or $PassEvidence){throw 'WeaponVisibilityProbe requires isolated15s neutral tracked stream'}
    $Controllers=$true;$MotionAim=$true;$BodyFollow=$true;$PoseObserve=$true;$Hands=$true;$MuzzleFire=$true;$TwoHandGrip=$true
}
if($BodyHolsterProbe){
    if($UntilHostExit -or $HostPid -ne 0 -or $Seconds -ne 15 -or -not $FrameChannel -or $OpticFilterObserve -or $PhysicalReloadRepeatProbe -or $PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadRoundProbe -or $ReloadHoldProbe -or $SightFlip -or $RigPulse -or $DeathProbe -or $EquipProbe -or $PassEvidence){throw 'BodyHolsterProbe requires isolated15s tracked body stream'}
    $BodyInventory=$true
}
if($PhysicalReloadRepeatProbe){$PhysicalReloadProbe=$true}
# Explicit read-only filter/resource observer; no ADS request or weapon switch.
if($BodyInventory){
    if($PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadHoldProbe -or $ReloadRoundProbe -or $RigPulse -or $EquipProbe -or $DeathProbe -or $OpticFilterObserve -or -not $FrameChannel){throw 'BodyInventory requires a real tracked stream without synthetic action/reload/optic diagnostics'}
    $PhysicalReload=$true; $TwoHandGrip=$true; $MuzzleFire=$true; $Hands=$true; $PoseObserve=$true; $BodyFollow=$true; $MotionAim=$true; $Controllers=$true
}
if($OpticFilterObserve){
    if($UntilHostExit -or $HostPid -ne 0 -or $Seconds -gt 15 -or -not $FrameChannel -or $PhysicalReload -or $PhysicalReloadProbe -or $ReloadHoldProbe -or $ReloadRoundProbe -or $ReloadRequestProbe -or $RigPulse -or $EquipProbe -or $DeathProbe){throw 'OpticFilterObserve requires an isolated <=15s stream without continuous/physical/reload/action diagnostics'}
    $Controllers=$true;$MotionAim=$true;$BodyFollow=$true;$PoseObserve=$true;$Hands=$true
}
# Candidate headset feature: requires native weapon-mode proof and tracked hands.
if($PumpHoldProbe){if($ReloadHoldProbe -or $ReloadRoundProbe -or $ReloadRequestProbe -or $Seconds -ne 15 -or -not $FrameChannel -or $HostPid -or $UntilHostExit -or $BodyInventory -or $SightFlip -or $PhysicalReload -or $PassEvidence){throw 'PumpHoldProbe requires isolated15s tracked stream'};$ReloadHoldProbe=$true;$Hands=$true;$TwoHandGrip=$true;$MuzzleFire=$true}
if(([int]$ReloadHoldProbe.IsPresent+[int]$ReloadRoundProbe.IsPresent+[int]$ReloadRequestProbe.IsPresent) -gt 1){throw 'Reload hold and round probes are mutually exclusive'}
if($PhysicalReloadProbe){$PhysicalReload=$true;if($UntilHostExit -or $Seconds -ne 30 -or $SightFlip){throw 'PhysicalReloadProbe requires isolated30s stream'}}
if($PhysicalReload){$Hands=$true;$TwoHandGrip=$true;$MuzzleFire=$true;if($ReloadHoldProbe -or $ReloadRoundProbe -or $ReloadRequestProbe -or $RigPulse -or $EquipProbe -or $DeathProbe -or -not $FrameChannel){throw 'PhysicalReload requires an explicit tracked stream without native diagnostic actions'}}
if($ReloadRequestProbe){$Hands=$true;$TwoHandGrip=$true;$MuzzleFire=$true;if($UntilHostExit -or $Seconds -ne 30 -or $RigPulse -or $EquipProbe -or $DeathProbe -or $SightFlip -or -not $FrameChannel){throw 'ReloadRequestProbe requires isolated30s stream'}}
if($ReloadHoldProbe -or $ReloadRoundProbe){$Hands=$true;if($UntilHostExit -or $Seconds -gt 15 -or $RigPulse -or $EquipProbe -or $DeathProbe -or $SightFlip){throw 'ReloadHoldProbe requires a bounded <=15s run without other diagnostic actions'}}
if($SightFlip){$TwoHandGrip=$true}
if($TwoHandGrip){$MuzzleFire=$true}
if($MuzzleFire){$Hands=$true}
if($EquipProbe){$MotionAim=$true;if($UntilHostExit){throw 'EquipProbe requires a bounded run'}}
if($DeathProbe){$MotionAim=$true;if($UntilHostExit){throw 'DeathProbe requires a bounded run'}}
if($Hands){$BodyFollow=$true;$PoseObserve=$true;if($RigPulse){throw 'Hands and RigPulse are separate modes'}}
if($RigPulse){$PoseObserve=$true;if($UntilHostExit){throw 'RigPulse requires a bounded run'}}
if($BodyFollow -or $PoseObserve){$MotionAim=$true}
if($MotionAim){$Controllers=$true}
if($UntilHostExit -and $HostPid -eq 0){throw 'UntilHostExit requires HostPid'}
if(($PassEvidence -or $UncapMirror -or $UntilHostExit -or $Controllers) -and [string]::IsNullOrEmpty($FrameChannel)){throw 'PassEvidence and UncapMirror require a tracked stream'}
$PSNativeCommandUseErrorActionPreference=$false
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1){throw 'Exactly one matching BC2 campaign process must already be running.'}
$target=$targets[0]
$null=$target.Handle
$crashPath=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2\crashreport.xml'
$crashBefore=if(Test-Path -LiteralPath $crashPath){(Get-Item -LiteralPath $crashPath).LastWriteTimeUtc}else{[DateTime]::MinValue}
$folder=Join-Path $PSScriptRoot ('reports\native-trace-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $folder | Out-Null
$payload=Join-Path $folder 'BC2NativeProbe.dll'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build\x86\BC2NativeProbe.dll') -Destination $payload
if($BodyInventory){
    $bodyAmmoCache=Join-Path $PSScriptRoot 'build\body-ammo-assets.fvrprop'
    if(Test-Path -LiteralPath $bodyAmmoCache -PathType Leaf){
        $bodyAmmoCacheBytes=(Get-Item -LiteralPath $bodyAmmoCache).Length
        if($bodyAmmoCacheBytes -gt 0 -and $bodyAmmoCacheBytes -le 16777216){
            Copy-Item -LiteralPath $bodyAmmoCache -Destination (Join-Path $folder 'body-ammo-assets.fvrprop')
        }
    }
}
$trace=Join-Path $PSScriptRoot 'build\x86\BC2NativeTrace.exe'
$report=Join-Path $folder 'native-trace.json'
$target.Modules | ForEach-Object {[ordered]@{name=$_.ModuleName;path=$_.FileName;base=$_.BaseAddress.ToInt64();size=$_.ModuleMemorySize}} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $folder 'modules-before.json')
[ordered]@{started_utc=[DateTime]::UtcNow.ToString('o');pid=$target.Id;game_path=$expected;game_sha256=(Get-FileHash -LiteralPath $expected).Hash;probe_sha256=(Get-FileHash -LiteralPath $payload).Hash;bootstrap_sha256=(Get-FileHash -LiteralPath $trace).Hash;duration_ms=$(if($UntilHostExit){$null}else{$Seconds*1000});until_host_exit=$UntilHostExit.IsPresent;host_pid=$HostPid;pass_evidence=$PassEvidence.IsPresent;uncap_mirror=$UncapMirror.IsPresent;controllers=[bool]$Controllers;motion_aim=[bool]$MotionAim;body_follow=[bool]$BodyFollow;pose_observe=[bool]$PoseObserve;rig_pulse=[bool]$RigPulse;hand_poses=[bool]$Hands;death_probe=[bool]$DeathProbe;equip_probe=[bool]$EquipProbe;muzzle_fire=[bool]$MuzzleFire;two_hand_grip=[bool]$TwoHandGrip;sight_flip=[bool]$SightFlip;reload_hold_probe=[bool]$ReloadHoldProbe;pump_hold_probe=[bool]$PumpHoldProbe;pump_native_accepted=$false;reload_round_probe=[bool]$ReloadRoundProbe;reload_request_probe=[bool]$ReloadRequestProbe;boat_head_aim=[bool]$BoatHeadAim;boat_head_fire=[bool]$BoatHeadFire;boat_head_aim_mode=$(if($BoatHeadFire){2}elseif($BoatHeadAim){1}else{0});boat_head_aim_headset_accepted=$false;xm8_magazine_probe=$Xm8MagazineProbe;magazine_reload=[bool]$MagazineReload;magazine_physical_probe=[bool]$MagazinePhysicalProbe;magazine_original_return_probe=[bool]$MagazineOriginalReturnProbe;magazine_full_return_probe=[bool]$MagazineFullReturnProbe;xm8_magazine_headset_verified=$false;physical_reload=[bool]$PhysicalReload;physical_reload_probe=[bool]$PhysicalReloadProbe;physical_reload_repeat_probe=[bool]$PhysicalReloadRepeatProbe;physical_reload_requested_rounds=$(if($PhysicalReloadRepeatProbe){2}else{1});optic_filter_observe=[bool]$OpticFilterObserve;body_inventory=[bool]$BodyInventory;weapon_visibility_probe=[bool]$WeaponVisibilityProbe;body_holster_probe=[bool]$BodyHolsterProbe;body_holster_diagnostic_permission=[bool]$BodyHolsterProbe;body_holster_fire_probe=[bool]$BodyHolsterXm8FireProbe;body_holster_diagnostic_profile=$(if($BodyHolsterXm8FireProbe){3}elseif($BodyHolsterXm8Probe){2}elseif($BodyHolsterProbe){1}else{0});body_holster_input_accepted=([bool]$BodyInventory -and -not $BodyHolsterProbe);body_holster_profile_mask=$(if($BodyInventory -and -not $BodyHolsterProbe){3}else{0});body_holster_headset_accepted=$false;empty_hands_enabled=([bool]$BodyInventory -and -not $BodyHolsterProbe);physical_reload_headset_verified=$false;sight_flip_headset_verified=$false;mode=$(if($CameraPulse){'single_original_draw_camera_pulse'}elseif($VisibilityPulse){'single_original_visibility_camera_pulse'}elseif($FrameChannel){'tracked_native_ipc_stream'}elseif($StereoBurst){'120_native_stereo_frames'}elseif($WorkPool){'single_frame_work_pool_expansion'}elseif($InitializeView){'inactive_initialized_native_view'}elseif($StereoOnce){'single_update_two_native_views'}elseif($OwnershipV2){'inactive_native_view_ownership_v2'}elseif($ViewLifecycle){'disabled_unsafe_lifecycle'}else{'native_pass_through_trace'})} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'manifest.json')
$probeArguments=@('--game',$config.game_path,'--dll',$payload,'--report',$report)
if($CameraPulse){$probeArguments+='--camera-pulse'}
if($VisibilityPulse){$probeArguments+='--visibility-pulse'}
if($OwnershipV2){$probeArguments+='--ownership-v2'}
if($StereoOnce){$probeArguments+='--stereo-once'}
if($InitializeView){$probeArguments+='--initialize-view'}
if($WorkPool){$probeArguments+='--work-pool'}
if($StereoBurst){$probeArguments+='--stereo-burst'}
if($FrameChannel){$probeArguments+=@('--stream',$FrameChannel)}
if($PassEvidence){$probeArguments+='--pass-evidence'}
if($Controllers){$probeArguments+='--controllers'}
if($MotionAim){$probeArguments+='--motion-aim'}
if($BodyFollow){$probeArguments+='--body-follow'}
if($PoseObserve){$probeArguments+='--pose-observe'}
if($RigPulse){$probeArguments+='--rig-pulse'}
if($MuzzleFire){$probeArguments+='--muzzle-fire'}
if($TwoHandGrip){$probeArguments+='--two-hand-grip'}
if($SightFlip){$probeArguments+='--sight-flip'}
if($PumpHoldProbe){$probeArguments+='--pump-hold-probe'}elseif($ReloadHoldProbe){$probeArguments+='--reload-hold-probe'}
if($ReloadRoundProbe){$probeArguments+='--reload-round-probe'}
if($ReloadRequestProbe){$probeArguments+='--reload-request-probe'}
if($BoatHeadAim){$probeArguments+='--boat-head-aim'}
if($BoatHeadFire){$probeArguments+='--boat-head-fire'}
if($MagazineReload){$probeArguments+='--magazine-reload'}
if($MagazinePhysicalProbe){$probeArguments+='--magazine-physical-probe'}
if($MagazineOriginalReturnProbe){$probeArguments+='--magazine-original-return-probe'}
if($MagazineFullReturnProbe){$probeArguments+='--magazine-full-return-probe'}
if($Xm8MagazineProbe -eq 'Insert'){$probeArguments+='--xm8-magazine-insert-probe'}
if($Xm8MagazineProbe -eq 'Cancel'){$probeArguments+='--xm8-magazine-cancel-probe'}
if($PhysicalReload){$probeArguments+='--physical-reload'}
if($PhysicalReloadRepeatProbe){$probeArguments+='--physical-reload-repeat-probe'}elseif($PhysicalReloadProbe){$probeArguments+='--physical-reload-probe'}
if($OpticFilterObserve){$probeArguments+='--optic-filter-observe'}
if($BodyInventory){$probeArguments+='--body-inventory'}
if($WeaponVisibilityProbe){$probeArguments+='--weapon-visibility-probe'}
if($BodyHolsterProbe){$probeArguments+=$(if($BodyHolsterXm8FireProbe){'--body-holster-xm8-fire-probe'}elseif($BodyHolsterXm8Probe){'--body-holster-xm8-probe'}else{'--body-holster-probe'})}
if($EquipProbe){$probeArguments+='--equip-probe'}
if($DeathProbe){$probeArguments+='--death-probe'}
if($Hands){$probeArguments+='--hand-poses'}
if($UncapMirror){$probeArguments+='--uncap-mirror'}
if($UntilHostExit){$probeArguments+=@('--until-host-exit',[string]$HostPid)}else{$probeArguments+=@('--seconds',[string]$Seconds)}
& $trace @probeArguments 1> (Join-Path $folder 'bootstrap.json') 2> (Join-Path $folder 'bootstrap-error.txt')
$traceExit=$LASTEXITCODE
$stability=[Diagnostics.Stopwatch]::StartNew()
while($stability.Elapsed.TotalSeconds -lt 15 -and -not $target.HasExited){Start-Sleep -Milliseconds 250;$target.Refresh()}
$newCrash=(Test-Path -LiteralPath $crashPath) -and ((Get-Item -LiteralPath $crashPath).LastWriteTimeUtc -gt $crashBefore)
if($newCrash){Copy-Item -LiteralPath $crashPath -Destination (Join-Path $folder 'crashreport-after.xml')}
$target.Refresh()
[ordered]@{completed_utc=[DateTime]::UtcNow.ToString('o');bootstrap_exit=$traceExit;stability_observation_ms=$stability.ElapsedMilliseconds;new_crash_report=$newCrash;game_exited=$target.HasExited;game_exit_code=$(if($target.HasExited){$target.ExitCode}else{$null});game_responding=$(if(-not $target.HasExited){$target.Responding}else{$false});module_resident_until_game_exit=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'completion.json')
Write-Output $folder
Get-Content -LiteralPath (Join-Path $folder 'bootstrap.json')
Get-Content -LiteralPath (Join-Path $folder 'completion.json')
if($traceExit -ne 0 -or ($target.HasExited -and -not $UntilHostExit) -or $newCrash){Get-Content -LiteralPath (Join-Path $folder 'bootstrap-error.txt');if(Test-Path -LiteralPath $report){Get-Content -LiteralPath $report -Raw | ConvertFrom-Json | Select-Object state,error,hooks_disabled,camera_pulse | ConvertTo-Json -Depth 4};throw "Native diagnostic or stability check failed (bootstrap exit $traceExit). Evidence: $folder"}
if($UntilHostExit -and $target.HasExited -and -not (Test-Path -LiteralPath $report -PathType Leaf)){Write-Output 'User closed the game; live telemetry retained.';return}
if($UntilHostExit -and $target.HasExited -and (Get-Item -LiteralPath $report).Length -eq 0){Write-Output 'User closed the game; live telemetry retained.';return}
$result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
[ordered]@{state=$result.state;hooks_disabled=$result.hooks_disabled;world_calls=$result.world_calls;prepare_calls=$result.prepare_calls;draw_calls=$result.draw_calls;complete_records=$result.complete_records} | ConvertTo-Json

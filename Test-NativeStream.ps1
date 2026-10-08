[CmdletBinding()]
param([ValidateRange(3,30)][int]$Seconds=8,[ValidateRange(8,240)][int]$Pairs=8,[switch]$ControlsObserve,[switch]$ControlsPulse,[switch]$ControlsTurn,[switch]$ControlsFire,[switch]$ControlsButtons,[switch]$ControlsAim,[switch]$ControlsRoomscale,[switch]$PoseObserve,[switch]$RigPulse,[switch]$ControlsHands,[switch]$ControlsHandIndependence,[switch]$ControlsTrackingRecovery,[switch]$ControlsEquip,[switch]$ControlsShotProbe,[switch]$ShotReload,[switch]$ControlsSupportGrip,[switch]$SupportReload,[switch]$SupportPrepareShots,[switch]$ReloadHoldProbe,[switch]$PumpHoldProbe,[switch]$ReloadRoundProbe,[switch]$ReloadRequestProbe,[switch]$PhysicalReloadProbe,[switch]$PhysicalReloadRepeatProbe,[switch]$OpticFilterObserve,[switch]$WeaponVisibilityProbe,[switch]$BodyHolsterProbe,[switch]$BodyHolsterXm8Probe,[switch]$BodyHolsterXm8FireProbe,[ValidateSet(-1,0,1)][int]$SupportPrimaryDirection=0,[switch]$MuzzleFireProbe,[ValidateRange(0,2)][int]$ShotPrimaryPulses=2,[ValidateSet(-1,1)][int]$ShotCycleDirection=-1,[switch]$ControlsWeaponCycle,[switch]$ControlsWeaponMode,[switch]$ControlsSightFlip,[switch]$SightStartSecondary,[switch]$ControlsUse,[switch]$ControlsVehicle,[switch]$DeathProbe,[ValidateRange(-0.5,0.5)][float]$RoomscaleStepX=0.4,[switch]$Asymmetric,[switch]$StaticPose,[switch]$Async,[switch]$FovSweep,[switch]$PassEvidence,[switch]$UncapMirror,[ValidateRange(1,200)][int]$RequestLifetimeMs=150)
$ErrorActionPreference='Stop'
if($PumpHoldProbe){
    $pumpAllowed=@('PumpHoldProbe','Seconds','Pairs','StaticPose','Async','UncapMirror')
    foreach($name in $PSBoundParameters.Keys){if($name -notin $pumpAllowed){throw "Pump diagnostic does not allow $name"}}
    if($Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async){throw 'Pump diagnostic requires15s/static/async240pairs'}
}

if($BodyHolsterXm8FireProbe){if($BodyHolsterXm8Probe -or $BodyHolsterProbe){throw 'Select one explicit holster diagnostic'};$BodyHolsterXm8Probe=$true}
if($BodyHolsterXm8Probe){$BodyHolsterProbe=$true}
if($WeaponVisibilityProbe -and $BodyHolsterProbe){throw 'Select one visibility/body diagnostic'}
if($WeaponVisibilityProbe){
    if($Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async -or $OpticFilterObserve -or $PhysicalReloadRepeatProbe -or $PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadHoldProbe -or $ReloadRoundProbe -or $SupportReload -or $SupportPrepareShots -or $ShotReload -or $MuzzleFireProbe -or $ControlsShotProbe -or $ControlsEquip -or $DeathProbe -or $ControlsSightFlip -or $SightStartSecondary -or $ControlsWeaponMode -or $ControlsWeaponCycle -or $ControlsUse -or $ControlsVehicle -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $RigPulse -or $ControlsTrackingRecovery -or $ControlsHandIndependence -or $ControlsSupportGrip -or $ControlsTurn -or $ControlsRoomscale -or $ControlsAim -or $ControlsHands -or $Asymmetric -or $FovSweep -or $PassEvidence -or $SupportPrimaryDirection -ne 0){throw 'WeaponVisibilityProbe requires isolated15s240-pair neutral static async stream'}
    $ControlsObserve=$true
}
if($BodyHolsterProbe){
    if($Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async -or $OpticFilterObserve -or $PhysicalReloadRepeatProbe -or $PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadHoldProbe -or $ReloadRoundProbe -or $SupportReload -or $SupportPrepareShots -or $ShotReload -or $MuzzleFireProbe -or $ControlsShotProbe -or $ControlsEquip -or $DeathProbe -or $ControlsSightFlip -or $SightStartSecondary -or $ControlsWeaponMode -or $ControlsWeaponCycle -or $ControlsUse -or $ControlsVehicle -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $RigPulse -or $ControlsTrackingRecovery -or $ControlsHandIndependence -or $ControlsSupportGrip -or $ControlsTurn -or $ControlsRoomscale -or $ControlsAim -or $ControlsHands -or $Asymmetric -or $FovSweep -or $PassEvidence -or $SupportPrimaryDirection -ne 0){throw 'BodyHolsterProbe requires isolated15s240-pair static async stream'}
    $ControlsObserve=$true
}
if($PhysicalReloadRepeatProbe){$PhysicalReloadProbe=$true}
if($OpticFilterObserve){
    if($Seconds -gt 15 -or $PhysicalReloadProbe -or $ReloadRequestProbe -or $ReloadHoldProbe -or $ReloadRoundProbe -or $SupportReload -or $SupportPrepareShots -or $ShotReload -or $MuzzleFireProbe -or $ControlsShotProbe -or $ControlsEquip -or $DeathProbe -or $ControlsSightFlip -or $SightStartSecondary -or $ControlsWeaponMode -or $ControlsWeaponCycle -or $ControlsUse -or $ControlsVehicle -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $RigPulse -or $ControlsTrackingRecovery -or $ControlsHandIndependence -or $ControlsSupportGrip -or $ControlsTurn -or $ControlsRoomscale -or $ControlsAim -or $ControlsHands -or $Asymmetric -or $FovSweep -or $SupportPrimaryDirection -ne 0){throw 'OpticFilterObserve requires a bounded neutral observation without action/motion fixtures'}
    # NativeTrace enables aim/body/pose/hands itself. The IPC peer supplies only
    # neutral tracked input; do not run the animated ControlsHands test script.
    $ControlsObserve=$true;$StaticPose=$true;$Async=$true
}
if(-not ($ReloadRequestProbe -or $PhysicalReloadProbe) -and $Seconds -gt 15){throw 'Non-request fixtures remain bounded to15s'}
if($ReloadRequestProbe -or $PhysicalReloadProbe){
    if($Seconds -ne 30 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async -or $SupportPrimaryDirection -ne 0 -or $ControlsShotProbe -or $ControlsEquip -or $DeathProbe -or $ControlsSightFlip -or $ControlsWeaponMode -or $ControlsWeaponCycle -or $ControlsUse -or $ControlsVehicle -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $RigPulse -or $ControlsTrackingRecovery -or $ControlsHandIndependence -or $ControlsTurn -or $ControlsRoomscale -or $ControlsAim -or $Asymmetric -or $FovSweep){throw 'ReloadRequestProbe requires isolated30s240-pair static async already selected SPAS fixture'}
    $SupportReload=$true
}
if($ControlsVehicle -and ($Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async -or $ControlsUse -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $ControlsTurn -or $ControlsAim -or $ControlsRoomscale -or $ControlsHands -or $ControlsHandIndependence -or $ControlsTrackingRecovery -or $ControlsEquip -or $ControlsShotProbe -or $ControlsSupportGrip -or $SupportReload -or $SupportPrepareShots -or $ReloadHoldProbe -or $ReloadRoundProbe -or $ControlsWeaponCycle -or $ControlsWeaponMode -or $ControlsSightFlip -or $DeathProbe -or $RigPulse -or $Asymmetric -or $FovSweep)){throw 'ControlsVehicle requires isolated15s240-pair static async campaign boat fixture'}
if(([int]$ReloadHoldProbe.IsPresent+[int]$ReloadRoundProbe.IsPresent+[int]$ReloadRequestProbe.IsPresent+[int]$PhysicalReloadProbe.IsPresent) -gt 1){throw 'Reload hold and round probes are mutually exclusive'}
if(($ReloadHoldProbe -or $ReloadRoundProbe) -and (-not $SupportReload -or $Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async -or $SupportPrimaryDirection -ne 0 -or $ControlsShotProbe -or $ControlsEquip -or $DeathProbe -or $ControlsSightFlip -or $ControlsWeaponMode -or $ControlsWeaponCycle -or $ControlsUse -or $ControlsFire -or $ControlsButtons -or $ControlsPulse -or $RigPulse -or $ControlsTrackingRecovery -or $ControlsHandIndependence -or $ControlsTurn -or $ControlsRoomscale -or $ControlsAim -or $Asymmetric -or $FovSweep)){throw 'ReloadHoldProbe requires the isolated15s/240pair static async SupportReload fixture on an already verified selected SPAS'}
if($SupportReload){$ControlsSupportGrip=$true}
if($SupportPrepareShots -and (-not $SupportReload -or $SupportPrimaryDirection -ne 0 -or $ControlsShotProbe -or $ControlsFire -or $ControlsPulse -or $DeathProbe -or $ControlsSightFlip -or $ControlsWeaponMode)){throw 'SupportPrepareShots requires SupportReload on an already verified selected weapon, without other action fixtures'}
if($SightStartSecondary -and -not $ControlsSightFlip){throw 'SightStartSecondary requires ControlsSightFlip'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2ExceptionWatch -ErrorAction SilentlyContinue){throw 'Another BC2 test is active'}
if(($ControlsWeaponMode -or $ControlsSightFlip) -and ($Seconds -ne 15 -or $Pairs -ne 240 -or -not $StaticPose -or -not $Async)){throw 'Weapon mode capture requires -Seconds 15 -Pairs 240 -StaticPose -Async'}
if($MuzzleFireProbe -and -not $ControlsShotProbe){throw 'MuzzleFireProbe requires ControlsShotProbe'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$targets=@(Get-Process BFBC2Game -ErrorAction Stop)
if($targets.Count -ne 1){throw 'Exactly one BC2 game must be running'}
$target=$targets[0]
if($ReloadRequestProbe -or $PhysicalReloadProbe){
    # Read-only and BEFORE any receiver/probe/input. The runtime still rechecks
    # ownership; this preflight does not authorize stale native state.
    $pythonRuntime=(Get-Command python.exe -CommandType Application -ErrorAction Stop).Source
    if($pythonRuntime -like '*\WindowsApps\*'){throw 'Use an installed Python interpreter on PATH'}
    if(-not (Test-Path -LiteralPath $pythonRuntime)){
        $pythonCommand=Get-Command python.exe -ErrorAction Stop
        if($pythonCommand.Source -like '*\WindowsApps\*'){throw 'Reload preflight needs an installed Python interpreter; Windows Store alias is not sufficient'}
        $pythonRuntime=$pythonCommand.Source
    }
    $preflightReport=Join-Path $PSScriptRoot ('reports\reload-preflight-'+$stamp+'.json')
    $preflightArgs=@((Join-Path $PSScriptRoot 'tools\preflight_reload_probe.py'),'--pid',[string]$target.Id,'--output',$preflightReport)
    if($SupportPrepareShots){$preflightArgs+='--prepare-shots'}
    # Repeated physical insertion needs the same two free slots/reserve minimum.
    if($ReloadRequestProbe -or $PhysicalReloadRepeatProbe){$preflightArgs+='--request-probe'}
    & $pythonRuntime @preflightArgs
    if($LASTEXITCODE -ne 0){throw "Reload preflight rejected the current weapon/ammo state; see $preflightReport"}
}
if($BodyHolsterProbe){
    $holsterPython=(Get-Command python.exe -CommandType Application -ErrorAction Stop).Source
    if($holsterPython -like '*\WindowsApps\*'){throw 'Use an installed Python interpreter on PATH'}
    if(-not (Test-Path -LiteralPath $holsterPython)){throw 'Body holster preflight needs the installed Python runtime'}
    $holsterBefore=Join-Path $PSScriptRoot ('reports\body-holster-preflight-'+$stamp+'.json')
    & $holsterPython (Join-Path $PSScriptRoot 'tools\preflight_body_holster_probe.py') --pid $target.Id --asset $(if($BodyHolsterXm8Probe){'XM8_sp_s'}else{'SPAS12_sp'}) --output $holsterBefore
    if($LASTEXITCODE -ne 0){throw "Body holster preflight rejected; no receiver or probe started. See $holsterBefore"}
    if($BodyHolsterXm8FireProbe){$counts=Get-Content -LiteralPath $holsterBefore -Raw | ConvertFrom-Json
        if($counts.summary.loaded -lt 4){throw 'Post-draw firing diagnostic needs at least four loaded XM8 rounds to exclude empty/automatic-reload behavior'}}
}
if($PumpHoldProbe){
    $pumpPython=(Get-Command python.exe -CommandType Application -ErrorAction Stop).Source
    if($pumpPython -like '*\WindowsApps\*'){throw 'Use an installed Python interpreter on PATH'}
    if(-not (Test-Path -LiteralPath $pumpPython)){throw 'Pump diagnostic requires installed Python for read-only preflight'}
    $pumpBefore=Join-Path $PSScriptRoot ('reports\pump-hold-preflight-'+$stamp+'.json')
    & $pumpPython (Join-Path $PSScriptRoot 'tools\preflight_body_holster_probe.py') --pid $target.Id --asset SPAS12_sp --output $pumpBefore
    if($LASTEXITCODE -ne 0){throw 'Pump preflight rejected; no diagnostic started'}
    $pumpCounts=Get-Content -Raw -LiteralPath $pumpBefore|ConvertFrom-Json
    if($pumpCounts.summary.loaded -lt 3){throw 'Pump trial needs at least3loaded SPAS rounds; last-round/auto-reload excluded'}
}
$receiverReport=Join-Path $PSScriptRoot ('reports\native-ipc-'+$stamp)
$receiverError=Join-Path $PSScriptRoot ('reports\native-ipc-'+$stamp+'-stderr.txt')
$receiverArgs=@('--output',$receiverReport,'--pairs',$Pairs,'--request-lifetime',$RequestLifetimeMs)
if($PumpHoldProbe){$receiverArgs+='--pump-hold-probe'}
if($SupportPrepareShots){$receiverArgs+='--support-prepare-shots'}
if($ReloadRequestProbe){$receiverArgs+='--reload-request-probe'}
if($PhysicalReloadProbe){$receiverArgs+='--physical-reload-probe'}
if($WeaponVisibilityProbe){$receiverArgs+='--weapon-visibility-probe'}
if($BodyHolsterProbe){$receiverArgs+='--body-holster-probe'}
if($ShotReload){if(-not $ControlsShotProbe){throw 'ShotReload requires ControlsShotProbe'};$receiverArgs+='--shot-reload'}
if($DeathProbe){$ControlsObserve=$true;$ControlsAim=$true;$receiverArgs+='--controls-death-probe'}
if($ControlsRoomscale){$ControlsObserve=$true;$receiverArgs+=@('--controls-roomscale','--roomscale-step-x',$RoomscaleStepX.ToString([Globalization.CultureInfo]::InvariantCulture))}
if($ControlsSightFlip){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-sight-flip';if($SightStartSecondary){$receiverArgs+='--sight-start-secondary'}}elseif($ControlsWeaponMode){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-weapon-mode'}
if($ControlsWeaponCycle){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-weapon-cycle'}
if($ControlsUse){$ControlsObserve=$true;$receiverArgs+='--controls-use'}
if($ControlsVehicle){$ControlsObserve=$true;$receiverArgs+='--controls-vehicle'}
if($ControlsShotProbe){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+=@('--controls-shot-probe','--shot-primary-pulses',[string]$ShotPrimaryPulses,'--shot-cycle-direction',[string]$ShotCycleDirection)}
if($ControlsEquip){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-equip'}
if($ControlsTrackingRecovery){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-tracking-recovery'}
if($ControlsHandIndependence){$ControlsHands=$true;$ControlsObserve=$true;$receiverArgs+='--controls-hand-independence'}
# The sight fixture already contains launcher support motion; enable the adapter
# without also scheduling the separate support-only receiver script.
if($ControlsSupportGrip){$ControlsHands=$true;$ControlsObserve=$true;if(-not $ControlsSightFlip){$receiverArgs+=@($(if($SupportReload){'--controls-support-reload'}else{'--controls-support-grip'}),'--support-primary-direction',[string]$SupportPrimaryDirection)}}
if($ControlsHands){$ControlsObserve=$true;$receiverArgs+='--controls-hands'}
if($RigPulse){$PoseObserve=$true;$receiverArgs+='--capture-poses'}
if($PoseObserve){$ControlsObserve=$true}
if($ControlsAim){$ControlsObserve=$true;$receiverArgs+='--controls-aim'}
if($ControlsButtons){$ControlsObserve=$true;$receiverArgs+='--controls-buttons'}
if($ControlsTurn){$ControlsObserve=$true;$receiverArgs+='--controls-turn'}
if($ControlsFire){$ControlsObserve=$true;$receiverArgs+='--controls-fire'}
if($ControlsPulse){$ControlsObserve=$true;$receiverArgs+='--controls-pulse'}elseif($ControlsObserve){$receiverArgs+='--controls-observe'}
if($Asymmetric){$receiverArgs+='--asymmetric'}
if($StaticPose){$receiverArgs+='--static-pose'}
if($Async){$receiverArgs+='--async'}
if($FovSweep){$receiverArgs+='--fov-sweep'}
$receiver=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList $receiverArgs -RedirectStandardOutput (Join-Path $PSScriptRoot ('reports\native-ipc-'+$stamp+'-stdout.txt')) -RedirectStandardError $receiverError -WindowStyle Hidden -PassThru
for($attempt=0;$attempt -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $receiverReport 'channel.txt'));$attempt++){Start-Sleep -Milliseconds 50;if($receiver.HasExited){throw 'Native receiver exited before ready'}}
$token=(Get-Content -LiteralPath (Join-Path $receiverReport 'channel.txt') -Raw).Trim()
$watchReport=Join-Path $PSScriptRoot ('reports\exception-watch-stream-'+$stamp)
$watch=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x86\BC2ExceptionWatch.exe') -ArgumentList @('--pid',$target.Id,'--seconds',$(if($ReloadRequestProbe -or $PhysicalReloadProbe){'60'}else{'40'}),'--output',$watchReport) -WindowStyle Hidden -PassThru
for($attempt=0;$attempt -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $watchReport 'ready.json'));$attempt++){Start-Sleep -Milliseconds 50;if($watch.HasExited){throw 'Exception watcher exited'}}
if(-not (Test-Path -LiteralPath (Join-Path $watchReport 'ready.json'))){throw 'Exception watcher not ready'}
Write-Output $receiverReport
try {& (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -Controllers:$ControlsObserve -MotionAim:$ControlsAim -BodyFollow:$ControlsRoomscale -PoseObserve:$PoseObserve -RigPulse:$RigPulse -Hands:$ControlsHands -MuzzleFireProbe:$MuzzleFireProbe -TwoHandGrip:$ControlsSupportGrip -SightFlip:$ControlsSightFlip -ReloadHoldProbe:$ReloadHoldProbe -PumpHoldProbe:$PumpHoldProbe -ReloadRoundProbe:$ReloadRoundProbe -ReloadRequestProbe:$ReloadRequestProbe -PhysicalReloadProbe:$PhysicalReloadProbe -PhysicalReloadRepeatProbe:$PhysicalReloadRepeatProbe -OpticFilterObserve:$OpticFilterObserve -WeaponVisibilityProbe:$WeaponVisibilityProbe -BodyHolsterProbe:($BodyHolsterProbe -and -not $BodyHolsterXm8FireProbe) -BodyHolsterXm8Probe:($BodyHolsterXm8Probe -and -not $BodyHolsterXm8FireProbe) -BodyHolsterXm8FireProbe:$BodyHolsterXm8FireProbe -EquipProbe:($ControlsEquip -or $ControlsShotProbe -or $ControlsWeaponMode) -DeathProbe:$DeathProbe -FrameChannel $token -Seconds $Seconds -PassEvidence:$PassEvidence -UncapMirror:$UncapMirror}
finally {if($receiver.WaitForExit(5000)){Get-Content -LiteralPath $receiverError -ErrorAction SilentlyContinue;if(Test-Path -LiteralPath (Join-Path $receiverReport 'result.json')){Get-Content -LiteralPath (Join-Path $receiverReport 'result.json')}}}
if($BodyHolsterProbe){
    $holsterAfter=Join-Path $receiverReport 'body-holster-postflight.json'
    & $holsterPython (Join-Path $PSScriptRoot 'tools\preflight_body_holster_probe.py') --pid $target.Id --asset $(if($BodyHolsterXm8Probe){'XM8_sp_s'}else{'SPAS12_sp'}) --output $holsterAfter
    if($LASTEXITCODE -ne 0){throw "Body holster postflight unavailable; keep count conservation unverified. See $holsterAfter"}
    Copy-Item -LiteralPath $holsterBefore -Destination (Join-Path $receiverReport 'body-holster-preflight.json')
}
if($PumpHoldProbe){
    & $pumpPython (Join-Path $PSScriptRoot 'tools\preflight_body_holster_probe.py') --pid $target.Id --asset SPAS12_sp --output (Join-Path $receiverReport 'pump-hold-postflight.json')
    if($LASTEXITCODE -ne 0){throw 'Pump postflight unavailable; native outcome unverified'}
    Copy-Item -LiteralPath $pumpBefore -Destination (Join-Path $receiverReport 'pump-hold-preflight.json')
}
if(-not $watch.WaitForExit(45000)){throw 'Exception watcher did not detach after its bounded observation'}
if(-not $receiver.HasExited -or $receiver.ExitCode -ne 0){throw 'Native receiver did not pass; see the report folder'}

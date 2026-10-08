[CmdletBinding()]
param([string]$ReportFolder,[ValidateRange(1,200)][int]$RequestLifetimeMs=150,[switch]$Controllers,[switch]$MotionAim,[switch]$BodyFollow,[switch]$Hands,[switch]$MuzzleFire,[switch]$TwoHandGrip,[switch]$SightFlip,[switch]$PhysicalReload,[switch]$MagazineReload,[switch]$BodyInventory,[switch]$BoatHeadAim,[switch]$BoatHeadFire,[switch]$Roomscale,[ValidateRange(0.01,1000)][float]$WorldUnitsPerMeter=1,[string]$Python)
$ErrorActionPreference='Stop'
if($BoatHeadFire -and -not $BoatHeadAim){throw 'BoatHeadFire requires explicit BoatHeadAim'}
if($BoatHeadAim){$BodyFollow=$true}
# Candidate headset feature: opt-in native launcher sight interaction.
if($MagazineReload){$PhysicalReload=$true}
if($BodyInventory){$PhysicalReload=$true}
if($PhysicalReload){$TwoHandGrip=$true}
if($SightFlip){$TwoHandGrip=$true}
if($TwoHandGrip){$MuzzleFire=$true}
if($MuzzleFire){$Hands=$true}
if($Hands){$BodyFollow=$true}
if($BodyFollow){$MotionAim=$true; $Roomscale=$true}
if($MotionAim){$Controllers=$true}
$PSNativeCommandUseErrorActionPreference=$false
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1){throw 'Leave BC2 running in campaign gameplay before starting VR.'}
$target=$targets[0];$null=$target.Handle
if(Get-Process BC2XrHost,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'A BC2 VR session or trace is already active.'}
if(-not $ReportFolder){$ReportFolder=Join-Path $PSScriptRoot ('reports\native-xr-session-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))}
$folder=[IO.Path]::GetFullPath($ReportFolder)
if(Test-Path -LiteralPath $folder){throw 'A new session report directory is required'}
New-Item -ItemType Directory -Path $folder | Out-Null
$channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json';$stopFile=Join-Path $folder 'stop.request'
$state=[ordered]@{state='starting';started_utc=[DateTime]::UtcNow.ToString('o');game_pid=$target.Id;host_pid=$null;supervisor_pid=$PID;stop_file=$stopFile;until_game_or_runtime_exit=$true;uncap_mirror=$true;controllers=[bool]$Controllers;motion_aim=[bool]$MotionAim;roomscale_reference=[bool]$Roomscale;world_units_per_meter=$WorldUnitsPerMeter;body_follow=[bool]$BodyFollow;collision_roomscale_headset_verified=$false;hand_translation=[bool]$Hands;hand_pose_headset_verified=$false;muzzle_fire=[bool]$MuzzleFire;two_hand_grip=[bool]$TwoHandGrip;sight_flip=[bool]$SightFlip;physical_reload=[bool]$PhysicalReload;magazine_reload=[bool]$MagazineReload;magazine_reload_headset_verified=$false;boat_head_aim=[bool]$BoatHeadAim;boat_head_fire=[bool]$BoatHeadFire;boat_head_aim_mode=$(if($BoatHeadFire){2}elseif($BoatHeadAim){1}else{0});boat_head_aim_headset_verified=$false;body_inventory=[bool]$BodyInventory;empty_hands_enabled=[bool]$BodyInventory;body_holster_profile_mask=$(if($BodyInventory){3}else{0});body_holster_headset_verified=$false;sight_flip_headset_verified=$false;two_hand_headset_verified=$false;muzzle_fire_headset_verified=$false;request_lifetime_ms=$RequestLifetimeMs;native_probe_started=$false;error=$null;binary_sha256=[ordered]@{}}
foreach($binary in @('build\x86\BC2NativeProbe.dll','build\x86\BC2NativeTrace.exe','build\x64\BC2XrHost.exe')){$state.binary_sha256[$binary]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $binary)).Hash}
$state.window_recovery=[ordered]@{startup=$null;shutdown=$null;original_hwnd=$null;python=$null}
$windowPython=$null
function Resolve-BC2WindowPython {
    if($Python){return [IO.Path]::GetFullPath($Python)}
    if($env:FVR_PYTHON){return [IO.Path]::GetFullPath($env:FVR_PYTHON)}
    foreach($command in @(Get-Command python.exe,python3.exe -CommandType Application -ErrorAction SilentlyContinue)){
        if($command.Source -and $command.Source -notmatch '[\\/]WindowsApps[\\/]'){return $command.Source}
    }
    throw 'Python was not found for BC2 window recovery; use -Python or FVR_PYTHON.'
}
function Invoke-BC2WindowRecovery([string]$Phase) {
    try {
        if(-not $script:windowPython){$script:windowPython=Resolve-BC2WindowPython}
        $state.window_recovery.python=$script:windowPython
        $windowReport=Join-Path $folder ('window-'+$Phase+'.json')
        $windowArgs=@('--pid',[string]$target.Id,'--show','--report',$windowReport,'--report-root',$folder)
        if($Phase -eq 'startup'){$windowArgs+='--left-monitor'}
        else{
            if(-not $state.window_recovery.original_hwnd){throw 'No original BC2 window was recorded at startup; cleanup will not select a replacement.'}
            $windowArgs+=@('--hwnd',[string]$state.window_recovery.original_hwnd)
        }
        $messages=@(& $script:windowPython (Join-Path $PSScriptRoot 'tools\game_window.py') @windowArgs 2>&1)
        if($LASTEXITCODE -ne 0){throw ('BC2 window recovery failed: '+($messages -join [Environment]::NewLine))}
        $result=Get-Content -LiteralPath $windowReport -Raw | ConvertFrom-Json
        $state.window_recovery[$Phase]=$result
        if($Phase -eq 'startup'){$state.window_recovery.original_hwnd=$result.after.hwnd}
    } catch {
        # Window recovery failure must neither steal focus nor mask native/XR
        # cleanup. Preserve it in the session report for an explicit follow-up.
        $state.window_recovery[$Phase]=[ordered]@{error=$_.Exception.Message;activation_requested=$false}
        Write-Warning ('BC2 '+$Phase+' window recovery: '+$_.Exception.Message)
    }
}
$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
$hostArgs=@('--ipc','--until-game-exit',$target.Id,'--request-lifetime',$RequestLifetimeMs,'--eye-width','1920','--eye-height','1080','--channel-file',('"'+$channel+'"'),'--ready-file',('"'+$ready+'"'),'--stop-file',('"'+$stopFile+'"'))
if($BodyInventory){$hostArgs+=@('--body-props-cache',('"'+(Join-Path $PSScriptRoot 'build\body-ammo-assets.fvrprop')+'"'))}
if($Controllers){$hostArgs+='--controllers'}
if($Roomscale){$hostArgs+='--roomscale'}
$hostArgs+=@('--world-units-per-meter',$WorldUnitsPerMeter.ToString([Globalization.CultureInfo]::InvariantCulture))
function Stop-BC2OwnedXrHost {
    param([Parameter(Mandatory=$true)][Diagnostics.Process]$Process,
          [Parameter(Mandatory=$true)][string]$StopFile,
          [ValidateRange(100,10000)][int]$WaitMilliseconds=10000)
    $result=[ordered]@{stop_requested=$false;graceful=$false;forced=$false;exited=$false;error=$null}
    try {
        # Retain the process handle before waiting; cleanup must not select a
        # different process if Windows later reuses the host PID.
        $null=$Process.Handle
        if($Process.HasExited){$result.exited=$true;return $result}
        try {
            [IO.File]::WriteAllText($StopFile,'Requested after BC2 session failure')
            $result.stop_requested=$true
        } catch {$result.error=$_.Exception.Message}
        if($Process.WaitForExit($WaitMilliseconds)){
            $result.graceful=$true;$result.exited=$true;return $result
        }
        # Preserve the previous bounded failure cleanup only after the host
        # had an opportunity to flush diagnostics through its normal exit.
        $Process.Kill();$result.forced=$true
        $result.exited=$Process.WaitForExit(2000)
    } catch {$result.error=$_.Exception.Message}
    return $result
}
$xr=$null
try {
    Invoke-BC2WindowRecovery 'startup'
    $xr=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') -ArgumentList $hostArgs -RedirectStandardOutput (Join-Path $folder 'host.json') -RedirectStandardError (Join-Path $folder 'runtime.stderr.log') -WindowStyle Hidden -PassThru
    $null=$xr.Handle
    $state.host_pid=$xr.Id
    $watch=[Diagnostics.Stopwatch]::StartNew()
    # The host creates this file before writing/closing its JSON. Existence
    # alone can expose an empty/partial document; wait for the complete fields.
    $requirements=$null
    while($null -eq $requirements -and -not $xr.HasExited -and $watch.Elapsed.TotalSeconds -lt 30){
        if(Test-Path -LiteralPath $ready){
            try {
                $candidateReady=Get-Content -LiteralPath $ready -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
                if($null -ne $candidateReady){
                    $fields=$candidateReady.PSObject.Properties.Name
                    if($fields -contains 'session_created' -and $fields -contains 'width' -and $fields -contains 'height'){$requirements=$candidateReady}
                }
            } catch {
                # A concurrent writer can still hold or be finishing the file.
                # A malformed/absent report remains bounded by the startup timeout.
            }
        }
        if($null -eq $requirements){Start-Sleep -Milliseconds 100;$xr.Refresh()}
    }
    if($null -eq $requirements){throw 'OpenXR session did not become ready; no probe attached.'}
    if(-not $requirements.session_created -or $requirements.width -ne 1920 -or $requirements.height -ne 1080){throw 'Unexpected XR session dimensions'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim()
    $state.state='running';$state.native_probe_started=$true
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
    Write-Output $folder
    & (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -Controllers:$Controllers -MotionAim:$MotionAim -BodyFollow:$BodyFollow -Hands:$Hands -MuzzleFire:$MuzzleFire -TwoHandGrip:$TwoHandGrip -SightFlip:$SightFlip -PhysicalReload:$PhysicalReload -MagazineReload:$MagazineReload -BodyInventory:$BodyInventory -BoatHeadAim:$BoatHeadAim -BoatHeadFire:$BoatHeadFire -FrameChannel $token -UntilHostExit -HostPid $xr.Id -UncapMirror
    $state.state='ended'
} catch {
    $state.state='failed';$state.error=$_.Exception.Message
    # Save host diagnostics even when the game/probe fails first. The owned
    # process receives the same graceful stop request as a normal test end.
    # Game and SteamVR processes are never targets of this failure cleanup.
    if($xr){$state.host_shutdown=Stop-BC2OwnedXrHost -Process $xr -StopFile $stopFile}
    throw
} finally {
    $target.Refresh();$state.game_exited=$target.HasExited;$state.ended_utc=[DateTime]::UtcNow.ToString('o')
    if($xr){$state.host_exited=$xr.HasExited}
    if(-not $state.game_exited){Invoke-BC2WindowRecovery 'shutdown'}
    else{$state.window_recovery.shutdown=[ordered]@{skipped='game_exited';activation_requested=$false}}
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'session.json')
}

[CmdletBinding()]
param([Parameter(Mandatory)][int]$ExpectedPid,
      [string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($ProjectRoot)
$config=Get-Content -LiteralPath (Join-Path $root 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1 -or $targets[0].Id -ne $ExpectedPid){throw 'Expected one exact already-running BC2 campaign process.'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2ExceptionWatch,BFVRPresenter -ErrorAction SilentlyContinue){throw 'Another BC2 diagnostic or BF2142 VR session is active.'}
$game=$targets[0]
if(@($game.Modules | Where-Object {$_.ModuleName -eq 'BC2NativeProbe.dll'}).Count){throw 'Use a fresh BC2 process for this gameplay scene fixture.'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$report=Join-Path $root ('reports\scene-gameplay-recovery-'+$stamp)
New-Item -ItemType Directory -Path $report | Out-Null
$receiverFolder=Join-Path $report 'receiver'
$receiverFile=Join-Path $root 'build\x64\BC2NativeIpcProbe.exe'
$receiver=$null
try {
    $receiverArguments=@('--output',('"'+$receiverFolder+'"'),'--pairs','240','--static-pose','--async','--scene-recovery-probe','--scene-controls-neutral')
    $receiver=Start-Process -FilePath $receiverFile -ArgumentList $receiverArguments -RedirectStandardOutput (Join-Path $report 'receiver.stdout.log') -RedirectStandardError (Join-Path $report 'receiver.stderr.log') -WindowStyle Hidden -PassThru
    $channel=Join-Path $receiverFolder 'channel.txt'
    $ready=[Diagnostics.Stopwatch]::StartNew()
    while(-not (Test-Path -LiteralPath $channel) -and $ready.ElapsedMilliseconds -lt 5000 -and -not $receiver.HasExited){Start-Sleep -Milliseconds 50;$receiver.Refresh()}
    if(-not (Test-Path -LiteralPath $channel)){throw 'Neutral gameplay receiver did not become ready; no native trace was started.'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim()
    if($token -notmatch '^[0-9a-f]{32}$'){throw 'Incomplete neutral gameplay receiver channel.'}
    [ordered]@{
        schema='fvr.bc2.scene_gameplay_neutral_monitor.v1';game_pid=$game.Id;receiver_pid=$receiver.Id
        duration_seconds=60;receiver_pairs=240;receiver_schedule_seconds=55
        neutral_controller_input=$true;scripted_actions=$false;checkpoint_actions_by_runner=$false
        expected_external_checkpoint_actions=2;headset_tested=$false;active_reload_cancellation_tested=$false;death_tested=$false
        feature_flags=@('SightFlip','PhysicalReload','MagazineReload','BodyInventory','BoatHeadAim','BoatHeadFire')
        source_actions=[ordered]@{stick_x=0;stick_y=0;trigger=0;squeeze=0;held=0;touch_active=0;touched=0}
        left_grip=@(-.2,-.25,-.45);right_grip=@(.15,-.10,-.20)
        receiver_arguments=$receiverArguments;receiver_sha256=(Get-FileHash -LiteralPath $receiverFile).Hash
        owner_evidence=@('gameplay.hand_ownership.events','gameplay.vehicle_gather_evidence','gameplay.reload_observer.samples')
        owner_scope='Initial on-foot hands/reload/body ownership, then actual checkpoint-selected role. Boat checkpoints prove infantry retirement and fresh driver identity; they do not prove on-foot reacquisition.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $report 'manifest.json')
    Write-Output $report
    # Checkpoint UI actions are performed separately by root. This script has
    # no keyboard/mouse injector, action schedules, native ammo writes or XR host.
    & (Join-Path $root 'Start-NativeTrace.ps1') -FrameChannel $token -Seconds 60 -SightFlip -PhysicalReload -MagazineReload -BodyInventory -BoatHeadAim -BoatHeadFire *> (Join-Path $report 'native.log')
    if(-not $receiver.HasExited -and -not $receiver.WaitForExit(5000)){throw 'Neutral gameplay receiver did not finish within its bound.'}
    if($receiver.ExitCode -ne 0){throw 'Neutral gameplay receiver failed; inspect its log.'}
    $result=Get-Content -LiteralPath (Join-Path $receiverFolder 'result.json') -Raw | ConvertFrom-Json
    if(-not $result.scene_controls_neutral -or $result.scene_neutral_input_samples -le 0){throw 'Receiver did not report the explicit neutral gameplay fixture.'}
    Write-Output 'Capture ended normally. Audit two actual checkpoint receipts, source zero actions, new native gameplay identities and both-eye images before accepting.'
} finally {
    if($receiver -and -not $receiver.HasExited){Stop-Process -Id $receiver.Id -ErrorAction SilentlyContinue}
}

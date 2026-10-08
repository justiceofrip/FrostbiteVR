[CmdletBinding()]
param([Parameter(Mandatory)][int]$ExpectedPid,
      [string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),
      [string]$Python='python.exe')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($ProjectRoot)
$config=Get-Content -LiteralPath (Join-Path $root 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1 -or $targets[0].Id -ne $ExpectedPid){throw 'Expected one exact already-running BC2 campaign process.'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2ExceptionWatch,BFVRPresenter -ErrorAction SilentlyContinue){throw 'Another BC2 diagnostic or BF2142 VR session is active.'}
if(@($targets[0].Modules | Where-Object {$_.ModuleName -eq 'BC2NativeProbe.dll'}).Count){throw 'Restart BC2 first; this cross-draw fixture needs the newly built native module.'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$report=Join-Path $root ('reports\body-cross-draw-'+$stamp)
New-Item -ItemType Directory -Path $report | Out-Null
$python=(Get-Command $Python -ErrorAction Stop).Source
& $python (Join-Path $root 'tools\preflight_body_holster_probe.py') --pid $ExpectedPid --output (Join-Path $report 'spas-preflight.json')
if($LASTEXITCODE -ne 0){throw 'Selected idle SPAS preflight failed; no receiver or native hook started.'}
$receiver=$null
try {
    $receiverFolder=Join-Path $report 'receiver'
    $receiverFile=Join-Path $root 'build\x64\BC2NativeIpcProbe.exe'
    $receiverArguments=@('--output',('"'+$receiverFolder+'"'),'--pairs','240','--static-pose','--async','--body-cross-draw-probe')
    $receiver=Start-Process -FilePath $receiverFile -ArgumentList $receiverArguments -RedirectStandardOutput (Join-Path $report 'receiver.stdout.log') -RedirectStandardError (Join-Path $report 'receiver.stderr.log') -WindowStyle Hidden -PassThru
    $channel=Join-Path $receiverFolder 'channel.txt'
    $ready=[Diagnostics.Stopwatch]::StartNew()
    while(-not (Test-Path -LiteralPath $channel) -and $ready.ElapsedMilliseconds -lt 5000 -and -not $receiver.HasExited){Start-Sleep -Milliseconds 50;$receiver.Refresh()}
    if(-not (Test-Path -LiteralPath $channel)){throw 'Receiver did not become ready; no native trace started.'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim()
    if($token -notmatch '^[0-9a-f]{32}$'){throw 'Incomplete receiver channel.'}
    [ordered]@{
        schema='fvr.bc2.body_cross_draw_monitor.v1';game_pid=$ExpectedPid;receiver_pid=$receiver.Id
        receiver_pairs=240;duration_seconds=15;receiver_schedule_seconds=14
        source='finite right-hand grip gestures; native adapter supplies all claims/receipts'
        precondition='SPAS selected; scoped XM8 available on left shoulder; no active reload'
        hide_gesture_ms=@(3000,4000);draw_gesture_ms=@(7000,8000)
        requested_fire=$false;requested_reload=$false;requested_weapon_scroll=$false
        native_holster_fixture=$false;headset_tested=$false
        feature_flags=@('BodyInventory','MagazineReload','SightFlip')
        receiver_sha256=(Get-FileHash -LiteralPath $receiverFile).Hash
        acceptance='Receiver completion alone is not acceptance. Audit SPAS Empty, changed XM8 owner, fresh paired Show receipt, new GunHold, final cleared action block and both-eye images.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $report 'manifest.json')
    Write-Output $report
    & (Join-Path $root 'Start-NativeTrace.ps1') -FrameChannel $token -Seconds 15 -BodyInventory -MagazineReload -SightFlip *> (Join-Path $report 'native.log')
    if(-not $receiver.HasExited -and -not $receiver.WaitForExit(5000)){throw 'Cross-draw receiver did not finish within its bound.'}
    if($receiver.ExitCode -ne 0){throw 'Cross-draw receiver failed; inspect its log.'}
    $result=Get-Content -LiteralPath (Join-Path $receiverFolder 'result.json') -Raw | ConvertFrom-Json
    if(-not $result.body_cross_draw_fixture){throw 'Receiver did not report the requested cross-draw fixture.'}
    Write-Output 'Capture complete; inspect body_holster_transitions and both-eye images before accepting.'
} finally {
    if($receiver -and -not $receiver.HasExited){Stop-Process -Id $receiver.Id -ErrorAction SilentlyContinue}
}

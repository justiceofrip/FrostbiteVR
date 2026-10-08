[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$config=Get-Content -LiteralPath (Join-Path $root 'config\local.json') -Raw | ConvertFrom-Json
$expected=[IO.Path]::GetFullPath((Join-Path $config.game_path 'BFBC2Game.exe'))
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1){throw 'One BC2 campaign process must already be running.'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2ExceptionWatch,BFVRPresenter -ErrorAction SilentlyContinue){throw 'Another BC2 diagnostic or BF2142 VR session is active.'}
$game=$targets[0]
if(@($game.Modules | Where-Object {$_.ModuleName -eq 'BC2NativeProbe.dll'}).Count){throw 'Use a fresh BC2 process for this level-lifecycle diagnostic.'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$report=Join-Path $root ('reports\scene-recovery-'+$stamp)
New-Item -ItemType Directory -Path $report | Out-Null
$receiverFolder=Join-Path $report 'receiver'
$receiver=$null
try {
    $receiver=Start-Process -FilePath (Join-Path $root 'build\x64\BC2NativeIpcProbe.exe') -ArgumentList @('--output',('"'+$receiverFolder+'"'),'--pairs','240','--static-pose','--async','--scene-recovery-probe') -RedirectStandardOutput (Join-Path $report 'receiver.stdout.log') -RedirectStandardError (Join-Path $report 'receiver.stderr.log') -WindowStyle Hidden -PassThru
    $channel=Join-Path $receiverFolder 'channel.txt'
    $ready=[Diagnostics.Stopwatch]::StartNew()
    while(-not (Test-Path -LiteralPath $channel) -and $ready.ElapsedMilliseconds -lt 5000 -and -not $receiver.HasExited){Start-Sleep -Milliseconds 50}
    if(-not (Test-Path -LiteralPath $channel)){throw 'Neutral receiver did not become ready.'}
    $token=(Get-Content -LiteralPath $channel -Raw).Trim()
    if($token -notmatch '^[0-9a-f]{32}$'){throw 'Incomplete neutral receiver channel.'}
    [ordered]@{schema='fvr.bc2.scene_recovery_monitor.v1';game_pid=$game.Id;receiver_pid=$receiver.Id;duration_seconds=60;receiver_pairs=240;controller_input=$false;game_actions=$false;headset_tested=$false;receiver_sha256=(Get-FileHash -LiteralPath (Join-Path $root 'build\x64\BC2NativeIpcProbe.exe')).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report 'manifest.json')
    Write-Output $report
    & (Join-Path $root 'Start-NativeTrace.ps1') -FrameChannel $token -Seconds 60 *> (Join-Path $report 'native.log')
    if(-not $receiver.HasExited -and -not $receiver.WaitForExit(5000)){throw 'Neutral receiver did not finish within its bound.'}
    if($receiver.ExitCode -ne 0){throw 'Neutral scene receiver failed; inspect its log.'}
    # Receiving pairs alone is not recovery acceptance. The review must find
    # actual owner replacements and fresh stereo images after each checkpoint.
    Write-Output 'Neutral capture completed; inspect owner cleanup, replacement views and post-reload images before declaring success.'
} finally {
    if($receiver -and -not $receiver.HasExited){Stop-Process -Id $receiver.Id -ErrorAction SilentlyContinue}
}

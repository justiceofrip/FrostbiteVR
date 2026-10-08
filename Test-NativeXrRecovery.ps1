[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Python)
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json
$expected=Join-Path $config.game_path 'BFBC2Game.exe'
$targets=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
if($targets.Count -ne 1){throw 'One matching campaign game must already be running'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2ExceptionWatch -ErrorAction SilentlyContinue){throw 'Another BC2 test is active'}
$target=$targets[0]
# Explicit zero-fire fixture: headset loss/reconnect and manual recenter.
$folder=Join-Path $PSScriptRoot ('reports\native-xr-recovery-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $folder | Out-Null
$channel=Join-Path $folder 'channel.txt';$ready=Join-Path $folder 'ready.json'
$fixture=Join-Path $PSScriptRoot 'build\x64\FvrNativeRecoveryTestXr.dll'
[ordered]@{headset_tested=$false;recovery_fixture=$true;runtime='explicit test-only DLL';fixture_sha256=(Get-FileHash -LiteralPath $fixture).Hash;native_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'build\x86\BC2NativeProbe.dll')).Hash;game_pid=$target.Id} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'manifest.json')
$watch=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x86\BC2ExceptionWatch.exe') -ArgumentList @('--pid',$target.Id,'--seconds','40','--output',(Join-Path $folder 'exception-watch')) -WindowStyle Hidden -PassThru
for($n=0;$n -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $folder 'exception-watch\ready.json'));$n++){Start-Sleep -Milliseconds 50;if($watch.HasExited){throw 'Exception watch failed'}}
if(-not (Test-Path -LiteralPath (Join-Path $folder 'exception-watch\ready.json'))){throw 'Exception watcher not ready'}
$xr=$null
try {
 $xr=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') -ArgumentList @('--ipc','--controllers','--roomscale','--seconds','25','--eye-width','1920','--eye-height','1080','--request-lifetime','150','--loader',$fixture,'--channel-file',$channel,'--ready-file',$ready) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'host.json') -RedirectStandardError (Join-Path $folder 'runtime.stderr.log')
 for($n=0;$n -lt 100 -and -not (Test-Path -LiteralPath $ready);$n++){Start-Sleep -Milliseconds 50;if($xr.HasExited){throw 'Test XR host failed'}}
 if(-not (Test-Path -LiteralPath $ready)){throw 'Test XR host readiness failed'}
 $token=(Get-Content -LiteralPath $channel -Raw).Trim()
 & (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -MuzzleFire -FrameChannel $token -UntilHostExit -HostPid $xr.Id -UncapMirror | Tee-Object -Variable traceOutput
 if(-not $xr.WaitForExit(5000) -or $xr.ExitCode -ne 0){throw 'XR host did not exit cleanly'}
 $prefix=Join-Path $PSScriptRoot 'reports\native-trace-'
 $native=@($traceOutput | Where-Object {$_ -is [string] -and $_.StartsWith($prefix)})[0]
 [ordered]@{native_report=$native;host_report=$folder;headset_tested=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'reports.json')
 if(-not $watch.WaitForExit(10000)){throw 'Exception watch did not complete'}
 & $Python (Join-Path $PSScriptRoot 'tools\check_tracking_recovery.py') $folder --output (Join-Path $folder 'validation.json')
 if($LASTEXITCODE -ne 0){throw 'Native tracking recovery evidence failed'}
 Get-Content -LiteralPath (Join-Path $folder 'host.json')
 Write-Output $folder
} finally {if($xr -and -not $xr.HasExited){Stop-Process -Id $xr.Id}}

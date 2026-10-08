[CmdletBinding()]
param([switch]$ResumeExisting,[switch]$ClickClose)
$ErrorActionPreference='Stop'
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeMenuProbe,BC2ExceptionWatch -ErrorAction SilentlyContinue){throw 'Another BC2 test is active'}
$targets=@(Get-Process BFBC2Game -ErrorAction Stop)
if($targets.Count -ne 1){throw 'Exactly one campaign process required'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$folder=Join-Path $PSScriptRoot ('reports\native-menu-'+$stamp)
$probeArgs=@('--output',$folder)
if($ResumeExisting){$probeArgs+='--resume-existing'}
if($ClickClose){$probeArgs+='--click-close'}
$receiver=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2NativeMenuProbe.exe') -ArgumentList $probeArgs -WindowStyle Hidden -PassThru -RedirectStandardError ($folder+'-stderr.txt')
for($n=0;$n -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $folder 'channel.txt'));$n++){Start-Sleep -Milliseconds 50;if($receiver.HasExited){throw 'Menu receiver exited before ready'}}
$token=(Get-Content -LiteralPath (Join-Path $folder 'channel.txt') -Raw).Trim()
$watchFolder=Join-Path $PSScriptRoot ('reports\exception-watch-menu-'+$stamp)
$watch=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x86\BC2ExceptionWatch.exe') -ArgumentList @('--pid',$targets[0].Id,'--seconds','40','--output',$watchFolder) -WindowStyle Hidden -PassThru
for($n=0;$n -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $watchFolder 'ready.json'));$n++){Start-Sleep -Milliseconds 50;if($watch.HasExited){throw 'Exception watcher exited'}}
Write-Output $folder
try {& (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -FrameChannel $token -Seconds 15 -UncapMirror}
finally {if($receiver.WaitForExit(5000)){Get-Content -LiteralPath (Join-Path $folder 'result.json') -ErrorAction SilentlyContinue};if(-not $watch.WaitForExit(10000)){Write-Warning 'Exception watcher is completing its bounded run'}}
if(-not $receiver.HasExited -or $receiver.ExitCode -ne 0){throw 'Native menu acceptance failed; inspect saved reports'}

[CmdletBinding()]
param([ValidateRange(5,45)][int]$Seconds=30,[ValidateRange(1,4096)][int]$EyeWidth=1920,[ValidateRange(1,4096)][int]$EyeHeight=1080,[ValidateRange(1,200)][int]$RequestLifetimeMs=150,[switch]$UncapMirror,[switch]$Controllers,[switch]$MotionAim,[switch]$BodyFollow,[switch]$Hands,[switch]$MuzzleFire,[switch]$TwoHandGrip,[switch]$SightFlip,[switch]$Roomscale,[ValidateRange(0.01,1000)][float]$WorldUnitsPerMeter=1)
$ErrorActionPreference='Stop'
# Candidate headset feature: opt-in native launcher sight interaction.
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
if($targets.Count -ne 1){throw 'Leave BC2 running in campaign gameplay before this bounded headset test.'}
$folder=Join-Path $PSScriptRoot ('reports\native-xr-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $folder | Out-Null
$channel=Join-Path $folder 'channel.txt'
$ready=Join-Path $folder 'ready.json'
$hostReport=Join-Path $folder 'host.json'
$runtimeLog=Join-Path $folder 'runtime.stderr.log'
$hostArgs=@('--ipc','--seconds',($Seconds+15),'--frame-budget','50','--request-lifetime',$RequestLifetimeMs,'--eye-width',$EyeWidth,'--eye-height',$EyeHeight,'--channel-file',('"'+$channel+'"'),'--ready-file',('"'+$ready+'"'))
if($Controllers){$hostArgs+='--controllers'}
if($Roomscale){$hostArgs+='--roomscale'}
$hostArgs+=@('--world-units-per-meter',$WorldUnitsPerMeter.ToString([Globalization.CultureInfo]::InvariantCulture))
Write-Output $folder
Write-Host 'Bounded BC2 world-view headset diagnostic. Physical scale, HUD, weapons and controllers still need validation.'
$xr=Start-Process -FilePath (Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe') -ArgumentList $hostArgs -RedirectStandardOutput $hostReport -RedirectStandardError $runtimeLog -WindowStyle Hidden -PassThru
$wait=[Diagnostics.Stopwatch]::StartNew()
while(-not (Test-Path -LiteralPath $ready) -and -not $xr.HasExited -and $wait.Elapsed.TotalSeconds -lt 30){Start-Sleep -Milliseconds 100;$xr.Refresh()}
if(-not (Test-Path -LiteralPath $ready)){Get-Content -LiteralPath $runtimeLog -Tail 12;throw "OpenXR session is not ready. No game probe was attached. Report: $folder"}
$session=Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json
if(-not $session.session_created -or $session.width -ne $EyeWidth -or $session.height -ne $EyeHeight){throw 'Unexpected XR session requirements'}
$token=(Get-Content -LiteralPath $channel -Raw).Trim()
try {& (Join-Path $PSScriptRoot 'Start-NativeTrace.ps1') -Controllers:$Controllers -MotionAim:$MotionAim -BodyFollow:$BodyFollow -Hands:$Hands -MuzzleFire:$MuzzleFire -TwoHandGrip:$TwoHandGrip -SightFlip:$SightFlip -FrameChannel $token -Seconds $Seconds -UncapMirror:$UncapMirror}
finally {if($xr.WaitForExit(5000)){Get-Content -LiteralPath $hostReport -ErrorAction SilentlyContinue}}
if($xr.HasExited -and $xr.ExitCode -ne 0){throw "OpenXR host failed; see $folder"}

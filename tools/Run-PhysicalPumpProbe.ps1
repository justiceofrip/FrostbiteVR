#requires -Version 7.0
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][int]$ExpectedPid,
 [Parameter(Mandatory=$true)][string]$ExpectedStartUtc,
 [Parameter(Mandatory=$true)][string]$GameDirectory,
 [Parameter(Mandatory=$true)][string]$BuildReceipt,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Python,
 [Parameter(Mandatory=$true)][string]$SetupPreflight,
 [ValidateSet(1,2,8)][int]$Cycles=2)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$run=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $run){throw 'Use a new output directory; preserve earlier evidence'}
$build=Get-Content -Raw -LiteralPath $BuildReceipt | ConvertFrom-Json
if($build.manual_pump_probe -ne $true -or $build.ammo_move_probe -or $build.ammo_refill_probe -or $build.ammo_resource_hands){throw 'Receipt must identify the isolated private manual-pump build'}
foreach($binary in @($build.binaries.probe,$build.binaries.trace,$build.binaries.receiver)){
 if(-not $binary.path -or (Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash -ne $binary.sha256){throw 'Compiled binary receipt mismatch'}
}
foreach($property in $build.source_files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $build.source_root $property.Name) -Algorithm SHA256).Hash -ne $property.Value){throw "Source changed after build: $($property.Name)"}
}
$game=[IO.Path]::GetFullPath((Join-Path $GameDirectory 'BFBC2Game.exe'))
$gameProcess=Get-Process -Id $ExpectedPid
$start=[DateTime]::Parse($ExpectedStartUtc).ToUniversalTime()
if($gameProcess.Path -ne $game -or $gameProcess.StartTime.ToUniversalTime() -ne $start){throw 'Game identity changed'}
if(@($gameProcess.Modules | Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count){throw 'Fresh game required'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 diagnostic session is active'}
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $BuildReceipt -Destination (Join-Path $run 'build-receipt.json')
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $run 'monitor-script.ps1')
& $Python -X utf8 -B $SetupPreflight --pid $ExpectedPid --output (Join-Path $run 'setup-before.json')
if($LASTEXITCODE -ne 0){throw 'Read-only setup preflight failed'}
$setup=Get-Content -Raw -LiteralPath (Join-Path $run 'setup-before.json') | ConvertFrom-Json
if(-not $setup.vehicle){
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_body_holster_probe.py') --pid $ExpectedPid --asset SPAS12_sp --output (Join-Path $run 'onfoot-before.json')
 if($LASTEXITCODE -ne 0){throw 'Selected on-foot SPAS preflight failed'}
}
function Start-OwnedHelper([string]$Executable,[string[]]$Arguments,[string]$Name){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Executable;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::Start($info)
 return @{Process=$process;Out=$process.StandardOutput.ReadToEndAsync();Error=$process.StandardError.ReadToEndAsync();Name=$Name}
}
function Save-OwnedHelper($Helper){
 if($Helper -and $Helper.Process.HasExited){
  [IO.File]::WriteAllText((Join-Path $run ($Helper.Name+'-stdout.txt')),$Helper.Out.GetAwaiter().GetResult())
  [IO.File]::WriteAllText((Join-Path $run ($Helper.Name+'-stderr.txt')),$Helper.Error.GetAwaiter().GetResult())
 }
}
$receiver=$null;$baseline=$null;$emptyPostflight=$null
try {
 $receiverDir=Join-Path $run 'receiver'
 $arguments=@('--output',$receiverDir,'--pairs','240','--static-pose','--async','--physical-pump-probe','--capture-poses')
 if($setup.vehicle){$arguments+='--pump-exit-vehicle'}
 if($Cycles -eq 8){$arguments+='--pump-empty-cycle'}
 $seconds=if($Cycles -eq 8){60}else{30}
 $receiver=Start-OwnedHelper $build.binaries.receiver.path $arguments 'receiver'
 for($attempt=0;$attempt -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $receiverDir 'channel.txt'));$attempt++){
  Start-Sleep -Milliseconds 50;if($receiver.Process.HasExited){throw 'Receiver exited before channel creation'}
 }
 $channel=(Get-Content -Raw -LiteralPath (Join-Path $receiverDir 'channel.txt')).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid IPC channel'}
 $gameProcess.Refresh();if($gameProcess.HasExited -or $gameProcess.StartTime.ToUniversalTime() -ne $start){throw 'Game changed before activation'}
 $baseline=Start-OwnedHelper $Python @('-X','utf8','-B',(Join-Path $PSScriptRoot 'capture_physical_pump_baseline.py'),'--source',$build.source_root,'--receiver',$receiverDir,'--pid',[string]$ExpectedPid) 'baseline'
 if($Cycles -eq 8){$emptyPostflight=Start-OwnedHelper $Python @('-X','utf8','-B',(Join-Path $PSScriptRoot 'capture_pump_empty_postflight.py'),'--source',$build.source_root,'--receiver',$receiverDir,'--pid',[string]$ExpectedPid) 'empty-postflight'}
 & $build.binaries.trace.path --game $GameDirectory --dll $build.binaries.probe.path --report (Join-Path $run 'native-trace.json') --stream $channel --seconds $seconds --physical-pump-probe --pump-cycles $Cycles 1> (Join-Path $run 'bootstrap.json') 2> (Join-Path $run 'bootstrap-error.txt')
 $traceExit=$LASTEXITCODE;$receiverExited=$receiver.Process.WaitForExit(5000);$baselineExited=$baseline.Process.WaitForExit(5000)
 Save-OwnedHelper $receiver;Save-OwnedHelper $baseline
 if($emptyPostflight){
  $postExited=$emptyPostflight.Process.WaitForExit(5000);Save-OwnedHelper $emptyPostflight
  $postflightExit=if($postExited){$emptyPostflight.Process.ExitCode}else{1}
  # Removing the mod restores stock empty reload. Preserve that separate state;
  # it is not the manual pump's in-session ammunition completion evidence.
  & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_body_holster_probe.py') --pid $ExpectedPid --asset SPAS12_sp --output (Join-Path $receiverDir 'game-after-detach.json')
 }else{
  & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_body_holster_probe.py') --pid $ExpectedPid --asset SPAS12_sp --output (Join-Path $receiverDir 'pump-postflight.json')
  $postflightExit=$LASTEXITCODE
 }
 $gameProcess.Refresh()
 @{pid=$ExpectedPid;start_utc=$ExpectedStartUtc;trace_exit=$traceExit;receiver_exited=$receiverExited;
   receiver_exit=$(if($receiverExited){$receiver.Process.ExitCode}else{$null});baseline_exited=$baselineExited;
   baseline_exit=$(if($baselineExited){$baseline.Process.ExitCode}else{$null});postflight_exit=$postflightExit;
   game_exited=$gameProcess.HasExited;responding=(!$gameProcess.HasExited -and $gameProcess.Responding);
   cycles_requested=$Cycles;mode='physical-pump-controller-candidate';headset_verified=$false;
   started_in_vehicle=$setup.vehicle;utc=[DateTime]::UtcNow.ToString('o')} |
   ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'completion.json') -Encoding utf8
 & $Python -X utf8 -B (Join-Path $PSScriptRoot 'audit_physical_pump_probe.py') --run $run --output (Join-Path $run 'pump-audit.json')
 $auditExit=$LASTEXITCODE
 Get-Content -Raw -LiteralPath (Join-Path $run 'completion.json')
 if($traceExit -ne 0 -or -not $receiverExited -or $receiver.Process.ExitCode -ne 0 -or $auditExit -ne 0){throw 'Physical pump trial incomplete; preserved report identifies the failed gate'}
} finally {
 foreach($helper in @($baseline,$emptyPostflight,$receiver)){
  if($helper){if(-not $helper.Process.HasExited){$helper.Process.Kill();$helper.Process.WaitForExit(3000) | Out-Null};Save-OwnedHelper $helper}
 }
}

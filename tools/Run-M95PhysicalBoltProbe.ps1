#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$ExpectedPid,
 [Parameter(Mandatory=$true)][string]$ExpectedStartUtc,
 [Parameter(Mandatory=$true)][string]$BuildReceipt,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Python,
 [ValidateRange(1,2)][int]$Cycles=2)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$run=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $run){throw 'Use a new output directory'}
$build=Get-Content -Raw -LiteralPath $BuildReceipt | ConvertFrom-Json
if(-not $build.manual_bolt_probe -or $build.m95_stock_shot_probe -or $build.manual_pump_probe -or $build.resource_pump_probe -or $build.resource_inventory_probe -or $build.resource_magazines -or $build.ammo_move_probe -or $build.ammo_resource_hands){throw 'Exact isolated M95 physical bolt build required'}
foreach($binary in @($build.binaries.probe,$build.binaries.trace,$build.binaries.receiver)){
 if((Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash -ne $binary.sha256){throw 'Frozen binary mismatch'}
}
foreach($file in $build.source_files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $build.source_root $file.Name) -Algorithm SHA256).Hash -ne $file.Value){throw "Source changed after build: $($file.Name)"}
}
$gameDirectory='G:\New folder (2)\Battlefield Bad Company 2'
$game=Get-Process -Id $ExpectedPid;$start=[DateTime]::Parse($ExpectedStartUtc).ToUniversalTime()
if($game.Path -ne (Join-Path $gameDirectory 'BFBC2Game.exe') -or $game.StartTime.ToUniversalTime() -ne $start){throw 'Game identity changed'}
if(@($game.Modules | Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count){throw 'Fresh NativeProbe session required'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,ProvisionRunner,EngineSetupRunner -ErrorAction SilentlyContinue){throw 'Another BC2 session is active'}
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $BuildReceipt -Destination (Join-Path $run 'build-receipt.json')
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $run 'monitor-script.ps1')
$capture=Join-Path $PSScriptRoot 'capture_m95_stock_shot_boundary.py'
& $Python -X utf8 -B $capture --pid $ExpectedPid --output (Join-Path $run 'before.json')
if($LASTEXITCODE -ne 0){throw 'M95 native preflight failed before injection'}
$before=Get-Content -Raw -LiteralPath (Join-Path $run 'before.json') | ConvertFrom-Json
if($before.samples[1].state.loaded -le $Cycles){throw 'This loaded-cycle trial requires a live round remaining after the requested shots'}
$receiver=$null;$out=$null;$err=$null
try {
 $receiverDir=Join-Path $run 'receiver'
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$build.binaries.receiver.path;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in @('--output',$receiverDir,'--pairs','240','--static-pose','--async','--physical-bolt-probe','--capture-poses')){$info.ArgumentList.Add($argument)}
 $receiver=[Diagnostics.Process]::Start($info);$out=$receiver.StandardOutput.ReadToEndAsync();$err=$receiver.StandardError.ReadToEndAsync()
 for($n=0;$n -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $receiverDir 'channel.txt'));$n++){
  Start-Sleep -Milliseconds 50;if($receiver.HasExited){throw 'Receiver exited before connection'}
 }
 $channel=(Get-Content -Raw -LiteralPath (Join-Path $receiverDir 'channel.txt')).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid channel'}
 $game.Refresh();if($game.HasExited -or $game.StartTime.ToUniversalTime() -ne $start){throw 'Game changed before injection'}
 & $build.binaries.trace.path --game $gameDirectory --dll $build.binaries.probe.path --report (Join-Path $run 'native-trace.json') --stream $channel --seconds 30 --physical-bolt-probe $Cycles 1> (Join-Path $run 'bootstrap.json') 2> (Join-Path $run 'bootstrap-error.txt')
 $traceExit=$LASTEXITCODE;$receiverExited=$receiver.WaitForExit(5000)
 & $Python -X utf8 -B $capture --pid $ExpectedPid --output (Join-Path $run 'after.json')
 $captureExit=$LASTEXITCODE;$game.Refresh()
 $countsMatched=$false;$ownerMatched=$false
 if($captureExit -eq 0){
  $after=Get-Content -Raw -LiteralPath (Join-Path $run 'after.json') | ConvertFrom-Json
  $b=$before.samples[1];$a=$after.samples[1]
  $countsMatched=($a.state.loaded -eq $b.state.loaded-$Cycles -and $a.state.reserve -eq $b.state.reserve)
  $ownerMatched=$true
  foreach($field in @('server_player','server_soldier','server_item','server_firing','weapon_data','firing_data','ammo_address')){
   if(($b.$field | ConvertTo-Json -Compress -Depth 10) -ne ($a.$field | ConvertTo-Json -Compress -Depth 10)){$ownerMatched=$false}
  }
  foreach($field in @('actor','player','weak','inventory','selected_slot','selected_weapon','items')){
   if(($b.client_owner.$field | ConvertTo-Json -Compress) -ne ($a.client_owner.$field | ConvertTo-Json -Compress)){$ownerMatched=$false}
  }
  if(($b.client_owner.flags -band 1) -ne ($a.client_owner.flags -band 1)){$ownerMatched=$false}
 }
 & $Python -X utf8 -B (Join-Path $PSScriptRoot 'audit_physical_bolt_probe.py') --run $run --output (Join-Path $run 'mechanism-status.json') --runtime-status $Cycles
 $mechanismStatusExit=$LASTEXITCODE
 @{pid=$ExpectedPid;start_utc=$ExpectedStartUtc;trace_exit=$traceExit;receiver_exited=$receiverExited;receiver_exit=$(if($receiverExited){$receiver.ExitCode}else{$null});postflight_exit=$captureExit;mechanism_status_exit=$mechanismStatusExit;mechanism_runtime_completed=($mechanismStatusExit -eq 0);
   game_exited=$game.HasExited;responding=(!$game.HasExited -and $game.Responding);headset_verified=$false;manual_bolt_admitted=$false;requested_cycles=$Cycles;postflight_counts_match=$countsMatched;postflight_owner_match=$ownerMatched;mechanism_audit_required=$true;utc=[DateTime]::UtcNow.ToString('o')} |
   ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'completion.json') -Encoding utf8
 if($traceExit -ne 0 -or -not $receiverExited -or $receiver.ExitCode -ne 0 -or $captureExit -ne 0 -or -not $countsMatched -or -not $ownerMatched -or $mechanismStatusExit -ne 0){throw 'Physical bolt trial incomplete; preserve recorded evidence'}
 Get-Content -Raw -LiteralPath (Join-Path $run 'completion.json')
} finally {
 if($receiver){
  if(-not $receiver.HasExited){$receiver.Kill();$receiver.WaitForExit(3000)|Out-Null}
  if($receiver.HasExited){[IO.File]::WriteAllText((Join-Path $run 'receiver-stdout.txt'),$out.GetAwaiter().GetResult());[IO.File]::WriteAllText((Join-Path $run 'receiver-stderr.txt'),$err.GetAwaiter().GetResult())}
 }
}

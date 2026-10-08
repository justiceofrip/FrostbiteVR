[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$ExpectedPid,
 [Parameter(Mandatory=$true)][string]$ExpectedStartUtc,
 [Parameter(Mandatory=$true)][string]$GameDirectory,
 [Parameter(Mandatory=$true)][string]$BuildReceipt,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Python,
 [string]$SetupPreflight='',
 [Parameter(Mandatory=$true)][string]$GeometryCache,
 [Parameter(Mandatory=$true)][string]$GeometrySha256,
 [switch]$ManualPump, [switch]$StartHeld,
 [string]$ExpectedAsset='XM8_sp_s')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$build=Get-Content -LiteralPath $BuildReceipt -Raw | ConvertFrom-Json
if($build.resource_inventory_probe -ne $true -or $build.resource_magazines -ne $true -or
 $build.manual_pump_probe -or $build.ammo_move_probe -or $build.ammo_refill_probe -or $build.ammo_resource_hands){
 throw 'Receipt must identify the isolated ordinary resource/inventory fixture build'}
if([bool]$build.resource_pump_probe -ne [bool]$ManualPump){throw 'Manual-pump mode must match the frozen build receipt'}
if($ExpectedAsset -notmatch '^[A-Za-z0-9_]{1,63}$'){throw 'Invalid expected asset'}
$gameExe=Join-Path $GameDirectory 'BFBC2Game.exe'
$start=[DateTime]::Parse($ExpectedStartUtc).ToUniversalTime()
function Assert-Game {
 $p=Get-Process -Id $ExpectedPid -ErrorAction Stop
 if($p.Path -ne $gameExe -or $p.StartTime.ToUniversalTime() -ne $start){throw 'Game identity changed'}
 return $p
}
$p=Assert-Game
if(@($p.Modules|Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count){throw 'Fresh game required'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 mod session is active'}
if(Test-Path -LiteralPath $Output){throw 'Never overwrite native evidence'}
if((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'){throw 'Game build differs'}
foreach($row in $build.source_files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $build.source_root $row.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.Value){throw "Source differs: $($row.Name)"}
}
foreach($role in 'probe','trace','receiver'){
 $binary=$build.binaries.$role
 if((Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binary.sha256){throw "Binary differs: $role"}
}
if($GeometrySha256 -notmatch '^[a-f0-9]{64}$' -or (Get-FileHash -LiteralPath $GeometryCache -Algorithm SHA256).Hash.ToLowerInvariant() -ne $GeometrySha256){throw 'Private body geometry differs'}
$null=New-Item -ItemType Directory -Path $Output
Copy-Item -LiteralPath $BuildReceipt -Destination (Join-Path $Output 'build-receipt.json')
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $Output 'monitor-script.ps1')
Copy-Item -LiteralPath $GeometryCache -Destination (Join-Path $Output 'body-ammo-assets.fvrprop')
@{source=$GeometryCache;sha256=$GeometrySha256;installed_assets_only=$true;distribution_allowed=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'geometry-cache-receipt.json')
$setupArguments=@('--pid',$ExpectedPid,'--output',(Join-Path $Output 'setup-before.json'))
if(-not $SetupPreflight){
 if(-not $StartHeld){throw 'A vehicle-start diagnostic needs its explicit setup preflight'}
 $SetupPreflight=Join-Path $PSScriptRoot 'preflight_resource_inventory.py'
 $setupArguments+=@('--expected-asset',$ExpectedAsset)
}
& $Python -X utf8 -B $SetupPreflight @setupArguments
if($LASTEXITCODE -ne 0){throw 'Read-only setup preflight failed; no injection'}
$setup=Get-Content -LiteralPath (Join-Path $Output 'setup-before.json') -Raw|ConvertFrom-Json
if($StartHeld){
 if($setup.vehicle){throw 'Held-start fixture requires an on-foot weapon'}
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_reload_probe.py') --pid $ExpectedPid --expected-asset $ExpectedAsset --prepare-shots --output (Join-Path $Output 'held-before.json')
 if($LASTEXITCODE -ne 0){throw 'Held-start exact family/count preflight failed before injection'}
} elseif(-not $setup.vehicle){throw 'Default fixture expects the verified campaign boat startup'}
$receiver=$null
try {
 $receiverDir=Join-Path $Output 'receiver'
 $arguments=@('--output',$receiverDir,'--pairs','240','--static-pose','--async','--resource-inventory-probe',
  '--request-lifetime','150','--capture-poses')
 if($StartHeld){$arguments+='--resource-start-held'}else{$arguments+=@('--weapon-cycle-exit-vehicle','--weapon-cycle-direction','-1')}
 $receiver=Start-Process -FilePath $build.binaries.receiver.path -ArgumentList $arguments -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput (Join-Path $Output 'receiver-stdout.txt') -RedirectStandardError (Join-Path $Output 'receiver-stderr.txt')
 for($attempt=0;$attempt -lt 100 -and -not(Test-Path -LiteralPath (Join-Path $receiverDir 'channel.txt'));$attempt++){
  Start-Sleep -Milliseconds 50
  if($receiver.HasExited){throw 'Receiver exited before IPC ready'}
 }
 $channel=(Get-Content -LiteralPath (Join-Path $receiverDir 'channel.txt') -Raw).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid IPC channel'}
 $p=Assert-Game
 & $build.binaries.trace.path --game $GameDirectory --dll $build.binaries.probe.path --report (Join-Path $Output 'native-trace.json') `
  --stream $channel --seconds 60 --inventory-reload-probe 1> (Join-Path $Output 'bootstrap.json') 2> (Join-Path $Output 'bootstrap-error.txt')
 $traceExit=$LASTEXITCODE
 $exited=$receiver.WaitForExit(5000)
 $p=Assert-Game
 $postArguments=@('--pid',$ExpectedPid,'--expected-asset',$ExpectedAsset,'--output',(Join-Path $Output 'reload-after.json'))
 if(-not $ManualPump){$postArguments+='--prepare-shots'}
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_reload_probe.py') @postArguments
 $postExit=$LASTEXITCODE
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/capture_reload_state.py') --pid $ExpectedPid --asset $ExpectedAsset --seconds 3 --interval-ms 100 --output (Join-Path $Output 'client-drain.json')
 $clientExit=$LASTEXITCODE
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/capture_reload_server.py') --pid $ExpectedPid --seconds 3 --interval-ms 100 --output (Join-Path $Output 'server-drain.json')
 $serverExit=$LASTEXITCODE
 $statusArguments=@('--trace',(Join-Path $Output 'native-trace.json'),'--output',(Join-Path $Output 'runtime-status.json'))
 if($ManualPump){$statusArguments+='--manual-pump'}
 & $Python -X utf8 -B (Join-Path $PSScriptRoot 'check_resource_inventory_status.py') @statusArguments
 $statusExit=$LASTEXITCODE
 $p.Refresh()
 @{pid=$ExpectedPid;start_utc=$ExpectedStartUtc;trace_exit=$traceExit;receiver_exited=$exited;
  receiver_exit=$(if($exited){$receiver.ExitCode}else{$null});postflight_exit=$postExit;
  client_drain_exit=$clientExit;server_drain_exit=$serverExit;runtime_status_exit=$statusExit;runtime_completed=($statusExit -eq 0);responding=$p.Responding;game_exited=$p.HasExited;
  utc=[DateTime]::UtcNow.ToString('o');headset_verified=$false;mode=$(if($ManualPump){'resource-pump-shell-shoulder-rifle-fire'}else{'ordinary-resource-rifle-shotgun-rifle'});manual_pump=[bool]$ManualPump;start_held=[bool]$StartHeld;expected_asset=$ExpectedAsset}|
  ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'completion.json')
 Get-Content -LiteralPath (Join-Path $Output 'completion.json') -Raw
 if($traceExit -or -not $exited -or $receiver.ExitCode -or $postExit -or $clientExit -or $serverExit -or $statusExit){exit 1}
} finally {
 if($receiver -and -not $receiver.HasExited){
  $owned=Get-Process -Id $receiver.Id -ErrorAction SilentlyContinue
  if($owned -and $owned.StartTime -eq $receiver.StartTime -and $owned.Path -eq $build.binaries.receiver.path){Stop-Process -Id $owned.Id}
 }
}

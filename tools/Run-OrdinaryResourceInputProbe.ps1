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
 [switch]$ManualPump, [switch]$StartHeld, [switch]$InterruptPump,
 [switch]$CampaignMagazine, [switch]$AllowNativeImages,
 [ValidateRange(1,4096)][int]$CaptureWidth=1920,[ValidateRange(1,4096)][int]$CaptureHeight=1080,
 [string]$ExpectedAsset='XM8_sp_s')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$build=Get-Content -LiteralPath $BuildReceipt -Raw | ConvertFrom-Json
if(-not $build.source_root -or -not $build.source_files -or -not @($build.source_files.PSObject.Properties).Count){throw 'Pinned compilation source map required'}
if(-not $build.ordinary_resource_input_probe -or -not $build.ordinary_manual_cycles -or -not $build.resource_magazines -or $build.ordinary_bolt_input_probe -or $build.resource_inventory_probe -or $build.resource_pump_probe -or $build.manual_bolt_probe -or $build.manual_pump_probe -or $build.ammo_move_probe -or $build.ammo_refill_probe -or $build.ammo_resource_hands){throw 'Exact ordinary resource input-only build required'}
if($CampaignMagazine){
 if($ManualPump -or $InterruptPump -or -not $StartHeld -or $SetupPreflight){throw 'Single magazine requires on-foot StartHeld, no pump/interruption/custom setup'}
 if(-not $build.campaign_magazine -or -not $build.native_cycle_recovery){throw 'Exact single-weapon campaign diagnostic required'}
 if(-not $build.cmake_cache -or (Get-FileHash -LiteralPath $build.cmake_cache.path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $build.cmake_cache.sha256){throw 'Pinned compiled CMake cache required'}
 $cache=[IO.File]::ReadAllText($build.cmake_cache.path)
 $flags=@{BC2_ORDINARY_MANUAL_CYCLES='ON';BC2_NATIVE_CYCLE_RECOVERY='ON';BC2_RESOURCE_MAGAZINES='ON';BC2_ORDINARY_RESOURCE_INPUT_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_EMPTY_PROBE='OFF';BC2_ORDINARY_SUPPORT_ONLY_PROBE='OFF'}
 foreach($entry in $flags.GetEnumerator()){
  $found=[regex]::Matches($cache,('(?m)^'+[regex]::Escape($entry.Key)+':BOOL=([^\r\n]+)\r?$'))
  if($found.Count -ne 1 -or $found[0].Groups[1].Value -cne $entry.Value){throw ('Compiled campaign flag differs: '+$entry.Key)}
 }
 foreach($entry in [regex]::Matches($cache,'(?m)^(BC2_[A-Z0-9_]*PROBE):BOOL=ON\r?$')){
  if($entry.Groups[1].Value -cnotin @('BC2_ORDINARY_RESOURCE_INPUT_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE')){throw ('Overlapping finite probe: '+$entry.Groups[1].Value)}
 }
}elseif(-not $ManualPump){throw 'This input driver requests a physical pump or explicit campaign-magazine mode'}
if([bool]$build.ordinary_resource_interruption_probe -ne [bool]$InterruptPump){throw 'Requested interruption scenario differs from the frozen build'}
if($InterruptPump -and -not $build.native_cycle_recovery){throw 'Interruption diagnostic requires native recovery'}
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
if(-not $AllowNativeImages){throw 'This native diagnostic writes up to eight raw frames; explicit -AllowNativeImages and verified capture dimensions are required. Receiver images stay disabled.'}
. (Join-Path $PSScriptRoot 'StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot (Split-Path $PSScriptRoot)
# Native capture dimensions come from the render target, not the desktop client.
# Reserve its existing hard maximum: eight RGBA frames of up to 4096 square.
$rawBytes=[long]8*4096*4096*4
$storage=Assert-FvrStorageBudget -RepositoryRoot (Split-Path $PSScriptRoot) -OutputPath $Output -Operation Session -RawCaptureBytes $rawBytes -PlannedBytes ($rawBytes+96MB)
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class FvrBenchWindow { [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;} [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd,out Rect rect); }'
function Assert-CaptureDimensions {
 $game=Assert-Game;$rect=[FvrBenchWindow+Rect]::new()
 if(-not [FvrBenchWindow]::GetClientRect($game.MainWindowHandle,[ref]$rect) -or ($rect.Right - $rect.Left) -ne $CaptureWidth -or ($rect.Bottom - $rect.Top) -ne $CaptureHeight){throw 'Actual game client dimensions differ from the requested test conditions'}
}
Assert-CaptureDimensions
foreach($row in $build.source_files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $build.source_root $row.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.Value){throw "Source differs: $($row.Name)"}
}
foreach($role in 'probe','trace','receiver'){
 $binary=$build.binaries.$role
 if((Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binary.sha256){throw "Binary differs: $role"}
}
if($GeometrySha256 -notmatch '^[a-f0-9]{64}$' -or (Get-FileHash -LiteralPath $GeometryCache -Algorithm SHA256).Hash.ToLowerInvariant() -ne $GeometrySha256){throw 'Private body geometry differs'}
$null=New-Item -ItemType Directory -Path $Output
$storage|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Output 'storage.json')
Copy-Item -LiteralPath $BuildReceipt -Destination (Join-Path $Output 'build-receipt.json')
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $Output 'monitor-script.ps1')
Copy-Item -LiteralPath $GeometryCache -Destination (Join-Path $Output 'body-ammo-assets.fvrprop')
@{source=$GeometryCache;sha256=$GeometrySha256;installed_assets_only=$true;distribution_allowed=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'geometry-cache-receipt.json')
$setupArguments=@('--pid',$ExpectedPid,'--output',(Join-Path $Output 'setup-before.json'))
if($CampaignMagazine){
 $SetupPreflight=Join-Path $PSScriptRoot 'preflight_reload_probe.py'
 $setupArguments+=@('--expected-asset',$ExpectedAsset,'--campaign-magazine')
}elseif(-not $SetupPreflight){
 if(-not $StartHeld){throw 'A vehicle-start diagnostic needs its explicit setup preflight'}
 $SetupPreflight=Join-Path $PSScriptRoot 'preflight_resource_inventory.py'
 $setupArguments+=@('--expected-asset',$ExpectedAsset)
}
& $Python -X utf8 -B $SetupPreflight @setupArguments
if($LASTEXITCODE -ne 0){throw 'Read-only setup preflight failed; no injection'}
$setup=Get-Content -LiteralPath (Join-Path $Output 'setup-before.json') -Raw|ConvertFrom-Json
if($CampaignMagazine){
 if(-not $setup.passed -or -not $setup.summary.campaign_magazine){throw 'Single selected three-copy preflight failed'}
}elseif($StartHeld){
 if($setup.vehicle){throw 'Held-start fixture requires an on-foot weapon'}
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_reload_probe.py') --pid $ExpectedPid --expected-asset $ExpectedAsset --prepare-shots --output (Join-Path $Output 'held-before.json')
 if($LASTEXITCODE -ne 0){throw 'Held-start exact family/count preflight failed before injection'}
} elseif(-not $setup.vehicle){throw 'Default fixture expects the verified campaign boat startup'}
$receiver=$null;$receiverClock=$null
function Wait-ReceiverBounded {
 if(-not $receiver -or -not $receiverClock){return $false}
 # Match the receiver's original 75-second loop plus a fixed retirement allowance.
 # Repeated calls share the original clock; they cannot renew its deadline.
 while(-not $receiver.HasExited -and $receiverClock.ElapsedMilliseconds -lt 85000){
  $remaining=85000-$receiverClock.ElapsedMilliseconds
  if($remaining -le 0){break}
  $null=$receiver.WaitForExit([int][Math]::Min(5000,$remaining))
 }
 return [bool]$receiver.HasExited
}
try {
 $receiverDir=Join-Path $Output 'receiver'
 $arguments=@('--output',$receiverDir,'--pairs','240','--static-pose','--async','--resource-inventory-probe',
  '--request-lifetime','150','--capture-poses','--no-image-files')
 if($StartHeld){$arguments+='--resource-start-held'}else{$arguments+=@('--weapon-cycle-exit-vehicle','--weapon-cycle-direction','-1')}
 $receiverClock=[Diagnostics.Stopwatch]::StartNew()
 $receiver=Start-Process -FilePath $build.binaries.receiver.path -ArgumentList $arguments -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput (Join-Path $Output 'receiver-stdout.txt') -RedirectStandardError (Join-Path $Output 'receiver-stderr.txt')
 for($attempt=0;$attempt -lt 100 -and -not(Test-Path -LiteralPath (Join-Path $receiverDir 'channel.txt'));$attempt++){
  Start-Sleep -Milliseconds 50
  if($receiver.HasExited){throw 'Receiver exited before IPC ready'}
 }
 $channel=(Get-Content -LiteralPath (Join-Path $receiverDir 'channel.txt') -Raw).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid IPC channel'}
 $p=Assert-Game;Assert-CaptureDimensions
 & $build.binaries.trace.path --game $GameDirectory --dll $build.binaries.probe.path --report (Join-Path $Output 'native-trace.json') `
  --stream $channel --seconds 60 --ordinary-resource-input-probe 1> (Join-Path $Output 'bootstrap.json') 2> (Join-Path $Output 'bootstrap-error.txt')
 $traceExit=$LASTEXITCODE
 $exited=Wait-ReceiverBounded
 $p=Assert-Game
 $postArguments=@('--pid',$ExpectedPid,'--expected-asset',$ExpectedAsset,'--output',(Join-Path $Output 'reload-after.json'))
 if(-not $ManualPump -and -not $CampaignMagazine){$postArguments+='--prepare-shots'}
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/preflight_reload_probe.py') @postArguments
 $postExit=$LASTEXITCODE
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/capture_reload_state.py') --pid $ExpectedPid --asset $ExpectedAsset --seconds 3 --interval-ms 100 --output (Join-Path $Output 'client-drain.json')
 $clientExit=$LASTEXITCODE
 & $Python -X utf8 -B (Join-Path $build.source_root 'tools/capture_reload_server.py') --pid $ExpectedPid --seconds 3 --interval-ms 100 --output (Join-Path $Output 'server-drain.json')
 $serverExit=$LASTEXITCODE
 $statusArguments=@('--trace',(Join-Path $Output 'native-trace.json'),'--output',(Join-Path $Output 'runtime-status.json'))
 if($ManualPump){$statusArguments+='--manual-pump'}
 if($CampaignMagazine){$statusArguments+='--campaign-magazine'}
 if($InterruptPump){$statusArguments+='--interrupted-pump'}
 & $Python -X utf8 -B (Join-Path $PSScriptRoot 'check_resource_inventory_status.py') @statusArguments
 $statusExit=$LASTEXITCODE
 $p.Refresh()
 @{pid=$ExpectedPid;start_utc=$ExpectedStartUtc;trace_exit=$traceExit;receiver_exited=$exited;
  receiver_exit=$(if($exited){$receiver.ExitCode}else{$null});postflight_exit=$postExit;
  client_drain_exit=$clientExit;server_drain_exit=$serverExit;runtime_status_exit=$statusExit;runtime_completed=($statusExit -eq 0);ordinary_native_dispatch=$true;responding=$p.Responding;game_exited=$p.HasExited;
  utc=[DateTime]::UtcNow.ToString('o');headset_verified=$false;native_reload_accepted=$false;independent_receipt_audit_required=$true;raw_images=$true;maximum_native_frames=8;raw_capture_budget=$rawBytes;campaign_magazine=[bool]$CampaignMagazine;mode=$(if($CampaignMagazine){'single-weapon-magazine-support-postfire'}elseif($InterruptPump){'resource-held-support-pump-interruption-shell-shoulder-rifle-fire'}elseif($ManualPump){'resource-pump-shell-shoulder-rifle-fire'}else{'ordinary-resource-rifle-shotgun-rifle'});manual_pump=[bool]$ManualPump;interrupted_pump=[bool]$InterruptPump;start_held=[bool]$StartHeld;expected_asset=$ExpectedAsset}|
  ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'completion.json')
 Get-Content -LiteralPath (Join-Path $Output 'completion.json') -Raw
 if($traceExit -or -not $exited -or $receiver.ExitCode -or $postExit -or $clientExit -or $serverExit -or $statusExit){exit 1}
} finally {
 $cleanup=@{schema='fvr.bc2.resource_receiver_cleanup.v1';forced_termination=$false;
  receiver_started=[bool]$receiver;receiver_exited=$false;receiver_exit=$null;
  receiver_identity_verified=$false;observation='not_started';budget_ms=85000}
 if($receiver){
  $cleanup.pid=$receiver.Id;$cleanup.start_utc=$receiver.StartTime.ToUniversalTime().ToString('o')
  try {
   $cleanup.receiver_exited=Wait-ReceiverBounded
   if($cleanup.receiver_exited){$cleanup.receiver_exit=$receiver.ExitCode;$cleanup.observation='retained_process_exited'}
   else {
    $owned=Get-Process -Id $receiver.Id -ErrorAction SilentlyContinue
    $cleanup.receiver_identity_verified=[bool]($owned -and $owned.StartTime -eq $receiver.StartTime -and $owned.Path -eq $build.binaries.receiver.path)
    $cleanup.observation=if($cleanup.receiver_identity_verified){'still_live_after_original_budget'}else{'unable_to_verify_original_process'}
   }
  }catch{$cleanup.observation='cleanup_observation_failed';$cleanup.error=$_.Exception.Message}
 }
 $cleanup|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'receiver-cleanup.json')
}

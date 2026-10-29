[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$ExpectedPid,
 [Parameter(Mandatory=$true)][string]$ExpectedStartUtc,
 [Parameter(Mandatory=$true)][string]$GameDirectory,
 [Parameter(Mandatory=$true)][string]$BuildReceipt,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Python,
 [switch]$Execute,
 [Parameter(Mandatory=$true)][string]$GeometryCache,
 [Parameter(Mandatory=$true)][string]$GeometrySha256,
 [switch]$AllowNativeImages,
 [ValidateRange(1,4096)][int]$CaptureWidth=1920,[ValidateRange(1,4096)][int]$CaptureHeight=1080,
 [Parameter(Mandatory=$true)][string]$ExpectedAsset,
 [Parameter(Mandatory=$true)][string]$BenchPlan,
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$BenchPlanSha256,
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-f0-9]{64}$')][string]$DefinitionId)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
if(-not $Execute){throw 'Use -Execute only after reviewed source/build qualification; nothing launched'}
$repo='G:\Unity\VRMMO\FrostbiteVR';$tools=Join-Path $repo 'tools'
. (Join-Path $PSScriptRoot 'CommonReviewed.ps1')
$prep=Assert-Preparation
$bound=Load (Join-Path $EvidenceRoot 'runtime-binding.json');CheckPin $bound.build;CheckPin $bound.admission
if($bound.schema -cne 'fvr.runtime02.magazine.runtime-binding.v1' -or $bound.source.sha256 -cne $workflow.source.sha256 -or $bound.workflow.sha256 -cne (Sha (Join-Path $RecipeRoot 'workflow-preparation.json'))){throw 'Wrong runtime02 binding'}
if([IO.Path]::GetFullPath($BuildReceipt) -ine $bound.build.path){throw 'Exact bound completed build required'}
$built=Assert-CompletedBuild $BuildReceipt $bound.build.sha256
$build=Get-Content -LiteralPath $BuildReceipt -Raw|ConvertFrom-Json
$runnerPrep=@{admission_helper=@{path=(Join-Path $PSScriptRoot 'Admission.py')}}
function Assert-BuildEvidencePin($v){Check $v.path $v.sha256;if((Get-Item -LiteralPath $v.path).Length -ne $v.bytes){throw 'Evidence size changed'}}
$ManualPump=$false;$InterruptPump=$false;$CampaignMagazine=$true;$StartHeld=$true;$SetupPreflight=''
if(-not $build.source_root -or -not $build.source_files -or -not @($build.source_files.PSObject.Properties).Count){throw 'Pinned compilation source map required'}
if(-not $build.ordinary_resource_input_probe -or -not $build.ordinary_manual_cycles -or -not $build.resource_magazines -or $build.ordinary_bolt_input_probe -or $build.resource_inventory_probe -or $build.resource_pump_probe -or $build.manual_bolt_probe -or $build.manual_pump_probe -or $build.ammo_move_probe -or $build.ammo_refill_probe -or $build.ammo_resource_hands){throw 'Exact ordinary resource input-only build required'}
if($ManualPump -or -not $CampaignMagazine -or $InterruptPump -or -not $StartHeld -or $SetupPreflight){throw 'Exact on-foot SingleMagazine mode required'}
if(-not $build.native_cycle_recovery){throw 'Existing native cycle recovery required'}
Assert-BuildEvidencePin $build.cmake_cache
$cache=[IO.File]::ReadAllText($build.cmake_cache.path)
$flags=@{BC2_ORDINARY_MANUAL_CYCLES='ON';BC2_NATIVE_CYCLE_RECOVERY='ON';BC2_RESOURCE_MAGAZINES='ON';BC2_ORDINARY_RESOURCE_INPUT_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE='ON';BC2_ORDINARY_RESOURCE_MAGAZINE_EMPTY_PROBE='OFF';BC2_ORDINARY_SUPPORT_ONLY_PROBE='OFF'}
foreach($entry in $flags.GetEnumerator()){
 $found=[regex]::Matches($cache,('(?m)^'+[regex]::Escape($entry.Key)+':BOOL=([^\r\n]+)\r?$'))
 if($found.Count -ne 1 -or $found[0].Groups[1].Value -cne $entry.Value){throw ('Compiled SingleMagazine flag differs: '+$entry.Key)}
}
foreach($entry in [regex]::Matches($cache,'(?m)^(BC2_[A-Z0-9_]*PROBE):BOOL=ON\r?$')){
 if($entry.Groups[1].Value -cnotin @('BC2_ORDINARY_RESOURCE_INPUT_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE','BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE')){throw ('Overlapping finite probe: '+$entry.Groups[1].Value)}
}
if([bool]$build.ordinary_resource_interruption_probe -ne [bool]$InterruptPump){throw 'Requested interruption scenario differs from the frozen build'}
if($InterruptPump -and -not $build.native_cycle_recovery){throw 'Interruption diagnostic requires native recovery'}
# Exact plan/registry/cache admission runs before any process access.
$admissionRaw=& $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' $runnerPrep.admission_helper.path --plan $BenchPlan --plan-sha256 $BenchPlanSha256 --definition $DefinitionId --asset $ExpectedAsset --build $BuildReceipt --geometry $GeometryCache --geometry-sha256 $GeometrySha256
if($LASTEXITCODE -ne 0){throw 'Exact saved-metadata admission failed; no process was opened'}
$admission=($admissionRaw -join "`n")|ConvertFrom-Json
if($admission.schema -cne 'fvr.r55.single-magazine.metadata-admission.v1' -or -not $admission.metadata_admitted -or $admission.runtime_admission_granted){throw 'Invalid metadata admission receipt'}
$gameExe=Join-Path $GameDirectory 'BFBC2Game.exe'
$start=[DateTime]::Parse($ExpectedStartUtc).ToUniversalTime()
function Assert-Game {
 Assert-Lease
 $p=Get-Process -Id $ExpectedPid -ErrorAction Stop
 if($p.Path -ne $gameExe -or $p.StartTime.ToUniversalTime() -ne $start){throw 'Game identity changed'}
 return $p
}
$p=Assert-Game
if($ExpectedAsset -cne 'Nambu100' -or $DefinitionId -cne '2e1cb951041973ccac4bedfa77714cdc2d69220f487efdcfe05eb222d8c1960c'){throw 'This continuation admits only exact Nambu100'}
if(@($p.Modules|Where-Object {$_.ModuleName -like 'BC2NativeProbe*'}).Count){throw 'Fresh game required'}
if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue){throw 'Another BC2 mod session is active'}
if(Test-Path -LiteralPath $Output){throw 'Never overwrite native evidence'}
$resolvedOutput=[IO.Path]::GetFullPath($Output);NoLinks $resolvedOutput
if([IO.Path]::GetDirectoryName($resolvedOutput) -ine $EvidenceRoot -or [IO.Path]::GetFileName($resolvedOutput) -cnotmatch '^run[0-9]{2}$'){throw 'Exact owned runNN output required'}
if((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'){throw 'Game build differs'}
if(-not $AllowNativeImages){throw 'This native diagnostic writes up to eight raw frames; explicit -AllowNativeImages and verified capture dimensions are required. Receiver images stay disabled.'}
. (Join-Path $tools 'StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$rawBytes=[long]8*4096*4096*4 # Native RTV/depth hard cap, not desktop client dimensions.
$storage=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $Output -Operation Session -RawCaptureBytes $rawBytes -PlannedBytes ($rawBytes+96MB) -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class FvrBenchWindow { [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;} [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd,out Rect rect); }'
function Assert-CaptureDimensions {
 $game=Assert-Game;$rect=[FvrBenchWindow+Rect]::new()
 if(-not [FvrBenchWindow]::GetClientRect($game.MainWindowHandle,[ref]$rect) -or ($rect.Right - $rect.Left) -ne $CaptureWidth -or ($rect.Bottom - $rect.Top) -ne $CaptureHeight){throw 'Actual game client dimensions differ from explicit raw-frame budget'}
}
Assert-CaptureDimensions
# The build restored normal repo sources. Only exact reviewed overlay paths
# may resolve through preserved compiled-source overrides; every other compiled
# input is still checked in its original repository location.
$expectedOverrides=@{};foreach($row in $prep.files){$expectedOverrides[$row.source]=$row.after}
if(@($build.source_overrides.PSObject.Properties).Count -ne $expectedOverrides.Count){throw 'Unexpected compiled-source override set'}
foreach($row in $build.source_overrides.PSObject.Properties){
 if(-not $expectedOverrides.ContainsKey($row.Name)){throw 'Unreviewed source override'}
 $expected=$expectedOverrides[$row.Name]
 if($row.Value.path -cne $expected.path -or $row.Value.sha256 -cne $expected.sha256 -or $row.Value.bytes -ne $expected.bytes){throw 'Compiled-source override differs from reviewed preparation'}
}
foreach($row in $build.source_files.PSObject.Properties){
 $override=$build.source_overrides.PSObject.Properties[$row.Name]
 $file=if($override){$override.Value.path}else{Join-Path $build.source_root $row.Name}
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row.Value){throw "Compiled source differs: $($row.Name)"}
}
foreach($role in 'probe','trace','receiver'){
 $binary=$build.binaries.$role
 if((Get-FileHash -LiteralPath $binary.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binary.sha256){throw "Binary differs: $role"}
}
if($GeometrySha256 -notmatch '^[a-f0-9]{64}$' -or (Get-FileHash -LiteralPath $GeometryCache -Algorithm SHA256).Hash.ToLowerInvariant() -ne $GeometrySha256){throw 'Private body geometry differs'}
Assert-Lease
$null=New-Item -ItemType Directory -Path $Output
$storage|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Output 'storage.json')
Copy-Item -LiteralPath $BuildReceipt -Destination (Join-Path $Output 'build-receipt.json')
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $Output 'monitor-script.ps1')
Copy-Item -LiteralPath $GeometryCache -Destination (Join-Path $Output 'body-ammo-assets.fvrprop')
@{source=$GeometryCache;sha256=$GeometrySha256;installed_assets_only=$true;distribution_allowed=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'geometry-cache-receipt.json')
# Existing single selected three-copy preflight. No paired weapon or SPAS assumption.
$admission|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $Output 'metadata-admission.json')
$setupArguments=@('--pid',$ExpectedPid,'--output',(Join-Path $Output 'inventory-setup-before.json'))
if($CampaignMagazine){
 $SetupPreflight=Join-Path $tools 'preflight_reload_probe.py'
 $setupArguments+=@('--expected-asset',$ExpectedAsset,'--campaign-magazine')
}elseif(-not $SetupPreflight){
 if(-not $StartHeld){throw 'A vehicle-start diagnostic needs its explicit setup preflight'}
 $SetupPreflight=Join-Path $tools 'preflight_resource_inventory.py'
 $setupArguments+=@('--expected-asset',$ExpectedAsset)
}
& $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' $SetupPreflight @setupArguments
if($LASTEXITCODE -ne 0){throw 'Read-only setup preflight failed; no injection'}
$setup=Get-Content -LiteralPath (Join-Path $Output 'inventory-setup-before.json') -Raw|ConvertFrom-Json
if($CampaignMagazine){
 if(-not $setup.passed -or -not $setup.summary.campaign_magazine){throw 'Single selected three-copy preflight failed'}
}elseif($StartHeld){
 if($setup.vehicle){throw 'Held-start fixture requires an on-foot weapon'}
 & $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' (Join-Path $build.source_root 'tools/preflight_reload_probe.py') --pid $ExpectedPid --expected-asset $ExpectedAsset --prepare-shots --output (Join-Path $Output 'held-before.json')
 if($LASTEXITCODE -ne 0){throw 'Held-start exact family/count preflight failed before injection'}
} elseif(-not $setup.vehicle){throw 'Default fixture expects the verified campaign boat startup'}
# Pin the actual observed path and capacity to the exact configured definition;
# the preflight's original coherent idle/finite/count checks remain unchanged.
if($setup.pid -ne $ExpectedPid -or $setup.summary.asset -cne $ExpectedAsset -or
 $setup.summary.capacity -ne $admission.configured_capacity){throw 'Observed selected profile/capacity differs from planned configuration'}
foreach($sample in $setup.samples){
 if($sample.asset_path -cne $admission.asset_path){throw 'Observed selected asset path differs from exact configured profile'}
}
$p=Assert-Game
if($setup.summary.loaded -ne $setup.summary.capacity){throw 'Freshly full configured magazine required for this episode'}
@{schema='fvr.r55.single_magazine_setup.v1';pid=$ExpectedPid;start_utc=$ExpectedStartUtc;
 expected_asset=$ExpectedAsset;definition=$DefinitionId;plan_path=$BenchPlan;plan_sha256=$BenchPlanSha256;
 on_foot_required=$true;paired_weapon_required=$false;preflight_passed=$true;
 preflight_path=(Join-Path $Output 'inventory-setup-before.json');
 preflight_sha256=(Get-FileHash -LiteralPath (Join-Path $Output 'inventory-setup-before.json')).Hash.ToLowerInvariant()
}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'setup-before.json')
$receiver=$null;$receiverClock=$null;$nativeCleanupConfirmed=$false
$traceInvocationPin=$null;$traceResultPin=$null;$traceFailureStage='receiver_setup'
function Save-TraceEvidence([ValidateSet('trace-invocation.json','trace-result.json','trace-failure.json')][string]$Name,$Value) {
 $path=Join-Path $Output $Name;$pending=$path+'.pending'
 if((Test-Path -LiteralPath $path) -or (Test-Path -LiteralPath $pending)){throw 'Preserve original trace invocation/result evidence'}
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12))
 if($bytes.Length -gt 64KB){throw 'Trace lifecycle receipt exceeds64KiB bound'}
 $file=[IO.FileStream]::new($pending,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,4096,[IO.FileOptions]::WriteThrough)
 try {$file.Write($bytes,0,$bytes.Length);$file.Flush($true)}finally{$file.Dispose()}
 # Same-directory publication is atomic; never overwrite or delete a failed preimage.
 [IO.File]::Move($pending,$path,$false)
}
function Trace-FileEvidence([string]$Path,[long]$MaximumBytes) {
 $e=@{path=$Path;exists=$false;sha256=$null;bytes=$null;status='missing';error=$null}
 try {
  if(Test-Path -LiteralPath $Path){
   $item=Get-Item -LiteralPath $Path;$e.exists=$true;$e.bytes=$item.Length
   if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){$e.status='reparse_refused'}
   elseif($item.Length -gt $MaximumBytes){$e.status='exceeds_hash_bound'}
   else {
    $length=$item.Length;$written=$item.LastWriteTimeUtc
    $hash=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $after=Get-Item -LiteralPath $Path
    if($after.Length -ne $length -or $after.LastWriteTimeUtc -ne $written){$e.status='changed_during_hash'}
    else {$e.sha256=$hash;$e.status='hashed_at_observation'}
   }
  }
 } catch {
  $e.status='observation_failed';$e.error=$_.Exception.Message
  if($e.error.Length -gt 2048){$e.error=$e.error.Substring(0,2048)}
 }
 return $e
}
function Wait-ReceiverBounded {
 if(-not $receiver -or -not $receiverClock){return $false}
 # The receiver's own fixed loop is 75 s, then feedback/GPU retirement.
 # A native early exit does not reset or shorten this original lifetime.
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
 $arguments+='--resource-start-held' # Existing on-foot SingleMagazine input path; no exit pulse.
 $receiverClock=[Diagnostics.Stopwatch]::StartNew()
 Assert-Lease
 $receiver=Start-Process -FilePath $build.binaries.receiver.path -ArgumentList $arguments -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput (Join-Path $Output 'receiver-stdout.txt') -RedirectStandardError (Join-Path $Output 'receiver-stderr.txt')
 for($attempt=0;$attempt -lt 100 -and -not(Test-Path -LiteralPath (Join-Path $receiverDir 'channel.txt'));$attempt++){
  Start-Sleep -Milliseconds 50
  if($receiver.HasExited){throw 'Receiver exited before IPC ready'}
 }
 $channel=(Get-Content -LiteralPath (Join-Path $receiverDir 'channel.txt') -Raw).Trim()
 if($channel -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid IPC channel'}
 $traceFailureStage='pre_trace_admission'
 $p=Assert-Game;Assert-CaptureDimensions
 Assert-Lease
 $traceOriginalGame=$p;$null=$traceOriginalGame.Handle
 $traceOriginalIdentity=@{pid=$ExpectedPid;path=$gameExe;start_utc=$ExpectedStartUtc;creation_filetime=[string]$start.ToFileTimeUtc();original_handle_retained=$true}
 $traceLogs=@{stdout=(Join-Path $Output 'bootstrap.json');stderr=(Join-Path $Output 'bootstrap-error.txt');native_report=(Join-Path $Output 'native-trace.json')}
 $traceArguments=@('--game',$GameDirectory,'--dll',$build.binaries.probe.path,'--report',$traceLogs.native_report,'--stream',$channel,'--seconds','60','--ordinary-resource-input-probe')
 $traceInvokedUtc=[DateTime]::UtcNow.ToString('o')
 Save-TraceEvidence 'trace-invocation.json' @{
  schema='fvr.runtime02.magazine.trace-invocation.v1';original_game=$traceOriginalIdentity
  build_receipt=$bound.build;executable=$build.binaries.trace;probe=$build.binaries.probe
  arguments=$traceArguments;logs=$traceLogs;working_directory=(Get-Location).Path
  receiver_cleanup_path=(Join-Path $Output 'receiver-cleanup.json');later_failure_path=(Join-Path $Output 'trace-failure.json')
  recorded_utc=$traceInvokedUtc;requested_native_seconds=60;call_method='synchronous_powershell_call_operator'
  trace_process_identity=$null;trace_process_identity_status='not_exposed_by_call_operator'
  remote_native_retirement_proven=$false;forced_termination=$false
 }
 $traceInvocationPin=Pin (Join-Path $Output 'trace-invocation.json')
 $traceExit=$null;$traceReturned=$false;$traceAttempted=$false;$traceException=$null
 $traceClock=[Diagnostics.Stopwatch]::StartNew()
 $traceFailureStage='trace_invocation'
 try {
  Assert-Lease
  $traceAttempted=$true
  & $build.binaries.trace.path @traceArguments 1> $traceLogs.stdout 2> $traceLogs.stderr
  $traceExit=$LASTEXITCODE;$traceReturned=$true
 } catch {
  $traceException=$_.Exception.Message
  if($traceException.Length -gt 2048){$traceException=$traceException.Substring(0,2048)}
  throw
 } finally {
  # Save the original CLI outcome before waiting for the receiver, parsing any
  # native report, or running postflight. CLI exit alone never retires native work.
  $traceReturnedUtc=[DateTime]::UtcNow.ToString('o');$traceClock.Stop()
  $gameAfter=@{observation='unavailable';has_exited=$null;exit_code=$null;error=$null}
  try {
   $gameAfter.has_exited=[bool]$traceOriginalGame.HasExited
   if($gameAfter.has_exited){$gameAfter.exit_code=$traceOriginalGame.ExitCode;$gameAfter.observation='original_process_exited'}
   else {$gameAfter.observation='original_process_alive'}
  } catch {
   $gameAfter.error=$_.Exception.Message
   if($gameAfter.error.Length -gt 2048){$gameAfter.error=$gameAfter.error.Substring(0,2048)}
  }
  $traceExitObserved=$traceReturned -and ($traceExit -is [int] -or $traceExit -is [long])
  Save-TraceEvidence 'trace-result.json' @{
   schema='fvr.runtime02.magazine.trace-result.v1';invocation=$traceInvocationPin;original_game=$traceOriginalIdentity
   build_receipt=$bound.build;executable=$build.binaries.trace;probe=$build.binaries.probe;arguments=$traceArguments;logs=$traceLogs
   invocation_utc=$traceInvokedUtc;observation_utc=$traceReturnedUtc;elapsed_ms=$traceClock.ElapsedMilliseconds
   trace_attempted=$traceAttempted;trace_returned=$traceReturned;cli_exit_observed=[bool]$traceExitObserved
   trace_exit=$(if($traceExitObserved){$traceExit}else{$null});invocation_exception=$traceException
   failure_stage=$(if($traceException){'trace_invocation'}elseif($traceExitObserved -and $traceExit -ne 0){'trace_cli_exit'}else{$null})
   failure_message=$(if($traceException){$traceException}elseif($traceExitObserved -and $traceExit -ne 0){'Original trace CLI returned nonzero'}else{$null})
   available_files=@{bootstrap=(Trace-FileEvidence $traceLogs.stdout 4MB);stderr=(Trace-FileEvidence $traceLogs.stderr 4MB);native_report=(Trace-FileEvidence $traceLogs.native_report 96MB)}
   receiver_cleanup_path=(Join-Path $Output 'receiver-cleanup.json');later_failure_path=(Join-Path $Output 'trace-failure.json')
   trace_process_identity=$null;trace_process_identity_status='not_exposed_by_call_operator'
   retained_game_after=$gameAfter;native_report_parsed=$false;remote_native_retirement_proven=$false;forced_termination=$false
  }
  $traceResultPin=Pin (Join-Path $Output 'trace-result.json')
 }
 $traceFailureStage='receiver_wait'
 $exited=Wait-ReceiverBounded
 $traceFailureStage='native_report_parse'
 $nativeResult=Get-Content -LiteralPath (Join-Path $Output 'native-trace.json') -Raw|ConvertFrom-Json
 $nativeCleanupConfirmed=$nativeResult.hooks_disabled -eq $true -and $nativeResult.gameplay.reload_flow.drained -eq $true -and $nativeResult.gameplay.reload_flow.in_flight -eq 0
 $traceFailureStage='postflight'
 $p=Assert-Game
 $postArguments=@('--pid',$ExpectedPid,'--expected-asset',$ExpectedAsset,'--output',(Join-Path $Output 'reload-after.json'))
 if(-not $ManualPump -and -not $CampaignMagazine){$postArguments+='--prepare-shots'}
 & $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' (Join-Path $build.source_root 'tools/preflight_reload_probe.py') @postArguments
 $postExit=$LASTEXITCODE
 & $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' (Join-Path $build.source_root 'tools/capture_reload_state.py') --pid $ExpectedPid --asset $ExpectedAsset --seconds 3 --interval-ms 100 --output (Join-Path $Output 'client-drain.json')
 $clientExit=$LASTEXITCODE
 & $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' (Join-Path $build.source_root 'tools/capture_reload_server.py') --pid $ExpectedPid --seconds 3 --interval-ms 100 --output (Join-Path $Output 'server-drain.json')
 $serverExit=$LASTEXITCODE
 & $Python -X utf8 -B -c 'import sys,runpy;from pathlib import Path;p=sys.argv[1];sys.argv=sys.argv[1:];sys.path.insert(0,str(Path(p).resolve().parent));runpy.run_path(p,run_name="__main__")' (Join-Path $PSScriptRoot 'CheckResult.py') --trace (Join-Path $Output 'native-trace.json') `
   --campaign-magazine --output (Join-Path $Output 'runtime-status.json')
 $statusExit=$LASTEXITCODE
 $p.Refresh()
 @{pid=$ExpectedPid;start_utc=$ExpectedStartUtc;trace_exit=$traceExit;receiver_exited=$exited;
  receiver_exit=$(if($exited){$receiver.ExitCode}else{$null});postflight_exit=$postExit;
  client_drain_exit=$clientExit;server_drain_exit=$serverExit;runtime_status_exit=$statusExit;runtime_completed=($statusExit -eq 0);ordinary_native_dispatch=$true;responding=$p.Responding;game_exited=$p.HasExited;
  scope='Nambu magazine only; not CHAT07 combined interactions or installed-runtime acceptance';utc=[DateTime]::UtcNow.ToString('o');headset_verified=$false;native_reload_accepted=$false;independent_receipt_audit_required=$true;raw_images=$true;maximum_native_frames=8;raw_capture_budget=$rawBytes;campaign_magazine=[bool]$CampaignMagazine;mode=$(if($CampaignMagazine){'single-weapon-magazine-support-postfire'}elseif($InterruptPump){'resource-held-support-pump-interruption-shell-shoulder-rifle-fire'}elseif($ManualPump){'resource-pump-shell-shoulder-rifle-fire'}else{'ordinary-resource-rifle-shotgun-rifle'});manual_pump=[bool]$ManualPump;interrupted_pump=[bool]$InterruptPump;start_held=[bool]$StartHeld;expected_asset=$ExpectedAsset}|
  ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'completion.json')
 Get-Content -LiteralPath (Join-Path $Output 'completion.json') -Raw
 if($traceExit -or -not $exited -or $receiver.ExitCode -or $postExit -or $clientExit -or $serverExit -or $statusExit){exit 1}
} catch {
 $failureMessage=$_.Exception.Message
 if($failureMessage.Length -gt 2048){$failureMessage=$failureMessage.Substring(0,2048)}
 Save-TraceEvidence 'trace-failure.json' @{
  schema='fvr.runtime02.magazine.trace-failure.v1';failure_stage=$traceFailureStage;failure_message=$failureMessage
  original_game=@{pid=$ExpectedPid;path=$gameExe;start_utc=$ExpectedStartUtc;creation_filetime=[string]$start.ToFileTimeUtc()}
  build_receipt=$bound.build;executable=$build.binaries.trace;probe=$build.binaries.probe
  invocation=$traceInvocationPin;result=$traceResultPin;receiver_cleanup_path=(Join-Path $Output 'receiver-cleanup.json')
  observed_utc=[DateTime]::UtcNow.ToString('o');remote_native_retirement_proven=$false;forced_termination=$false
 }
 throw
} finally {
 # Never kill a receiver to manufacture a cleanup pass. Its original fixed
 # lifetime remains in effect after a failed injection/trace. Record unknowns.
 $cleanup=@{schema='fvr.bc2.boat_receiver_cleanup.v1';native_cleanup_confirmed=[bool]$nativeCleanupConfirmed;
   forced_termination=$false;receiver_started=[bool]$receiver;receiver_exited=$false;
   receiver_identity_verified=$false;observation='not_started';receiver_exit=$null;budget_ms=85000;
   trace_invocation=$traceInvocationPin;trace_result=$traceResultPin;trace_failure_path=(Join-Path $Output 'trace-failure.json')}
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
  } catch {$cleanup.observation='observation_unavailable'}
 }
 $cleanup|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Output 'receiver-cleanup.json')
}

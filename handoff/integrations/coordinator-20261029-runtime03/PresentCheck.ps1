#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^scene[0-9]{2}$')][string]$SceneName)
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$scene=Join-Path $repo ('reports/coordinator-20261028-runtime02/'+$SceneName)
$output=Join-Path $scene 'present01'
$exe=Join-Path $repo 'reports/coordinator-20261029-runtime03/present-tool/PresentMon-2.6.0-x64.exe'
$expectedTool='b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'
$gamePath='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
$gameHash='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
$logman=Join-Path $env:SystemRoot 'System32/logman.exe'
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save($name,$value){
 $path=Join-Path $output $name
 if(Test-Path -LiteralPath $path){throw "Preserve existing evidence: $name"}
 [IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
function Lease {
 $lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json
 if($lease.owner_token -cne 'campaign-coordinator-20261028-r1-runtime02-91aa4c54d34a41219b8483ccdd7629cb' -or
    $lease.run -cne 'coordinator-20261028-runtime02'){throw 'Integration ownership differs'}
}
function OriginalGame {
 $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
 if($games.Count -ne 1){throw 'Expected exactly one original stock game'}
 $game=$games[0]
 if($game.Id -ne $identity.pid -or $game.Path -cne $gamePath -or
    [string]$game.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$identity.creation_filetime){throw 'Original process lifetime differs'}
 return $game
}
function Assert-PresentPlacementProof($identity,$placement,$observation,[long]$now){
 if(-not $placement.fully_contained -or $placement.original_game.pid -ne $identity.pid -or
  [string]$placement.original_game.creation_filetime -cne [string]$identity.creation_filetime -or
  $placement.original_game.path -cne $identity.path){throw 'Matching original-graphics full-placement proof required'}
 $window=$observation.window;$previous=$placement.after
 $age=$now-[long]$observation.observed_tick_ms
 if(-not $observation.observation_only -or $age -lt 0 -or $age -gt 60000 -or
  [long]$placement.observed_tick_ms -le 0 -or [long]$observation.observed_tick_ms -lt [long]$placement.observed_tick_ms){throw 'Window observation is stale or precedes placement'}
 if($window.pid -ne $identity.pid -or [string]$window.creation_filetime -cne [string]$identity.creation_filetime -or
  -not $window.read_only -or -not $window.visible -or $window.minimized -or -not $window.foreground_matches){throw 'Original visible foreground window required'}
 foreach($key in 'hwnd','pid','creation_filetime','style','visible','minimized','foreground','foreground_owner','foreground_thread'){
  if($window.$key -cne $previous.$key){throw ('Window identity/state differs after placement: '+$key)}
 }
 foreach($key in 'rect','client_rect','left_monitor'){
  foreach($rectangle in @($window.$key,$previous.$key)){
   foreach($side in 'left','top','right','bottom'){
    $value=$rectangle.$side
    if(($value -isnot [int] -and $value -isnot [long]) -or $value -lt [int]::MinValue -or $value -gt [int]::MaxValue){throw 'Window geometry requires bounded integer coordinates'}
   }
   if([long]$rectangle.right -le [long]$rectangle.left -or [long]$rectangle.bottom -le [long]$rectangle.top){throw 'Window geometry requires positive extents'}
  }
  foreach($side in 'left','top','right','bottom'){
   if($window.$key.$side -ne $previous.$key.$side){throw ('Window geometry differs after placement: '+$key+'.'+$side)}
  }
 }
 if($window.rect.left -lt $window.left_monitor.left -or $window.rect.top -lt $window.left_monitor.top -or
  $window.rect.right -gt $window.left_monitor.right -or $window.rect.bottom -gt $window.left_monitor.bottom){throw 'Fully contained original window required'}
}
function Sessions {
 $info=[Diagnostics.ProcessStartInfo]::new($logman)
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $info.ArgumentList.Add('query');$info.ArgumentList.Add('-ets')
 $query=[Diagnostics.Process]::Start($info)
 $stdout=$query.StandardOutput.ReadToEndAsync();$stderr=$query.StandardError.ReadToEndAsync()
 if(-not $query.WaitForExit(3000)){
  $query.Kill()
  if(-not $query.WaitForExit(2000)){throw ('Owned logman query remains: '+$query.Id)}
  throw 'ETW query timed out; cleanup state is unconfirmed'
 }
 if(-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout,$stderr),1000)){throw 'ETW query output did not finish'}
 if($query.ExitCode -ne 0){throw ('Cannot enumerate ETW sessions: '+$stderr.Result)}
 return @($stdout.Result -split '\r?\n')
}
function HasSession($lines){@($lines|Where-Object {$_ -match ('^\s*'+[regex]::Escape($session)+'\s')}).Count -gt 0}
function StartTool([string[]]$arguments,[string]$prefix){
 Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $output -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput (Join-Path $output ($prefix+'-stdout.txt')) -RedirectStandardError (Join-Path $output ($prefix+'-stderr.txt'))
}
Lease
if(Test-Path -LiteralPath $output){throw 'Preserve prior Present evidence'}
if(Test-Path -LiteralPath (Join-Path $scene 'finish.json')){throw 'Scene already finished'}
if((Sha $exe) -cne $expectedTool -or (Sha $gamePath) -cne $gameHash){throw 'Reviewed binary differs'}
$identity=Get-Content -LiteralPath (Join-Path $scene 'process.json') -Raw|ConvertFrom-Json
if($identity.path -cne $gamePath -or $identity.sha256 -cne $gameHash -or $identity.mod_injected){throw 'Stock launch receipt differs'}
$placement=Get-Content -LiteralPath (Join-Path $scene 'original-graphics-placement.json') -Raw|ConvertFrom-Json
$observation=Get-Content -LiteralPath (Join-Path $scene 'window-before-present.json') -Raw|ConvertFrom-Json
Assert-PresentPlacementProof $identity $placement $observation ([Environment]::TickCount64)
$game=OriginalGame
if(@($game.Modules|Where-Object {$_.ModuleName -match 'BC2Native|FrostbiteVR'}).Count){throw 'Stock-only trace refused an attached module'}
if(@(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeMenuProbe,'PresentMon*' -ErrorAction SilentlyContinue).Count){throw 'Shared helper active'}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
 $principal.IsInRole([Security.Principal.SecurityIdentifier]::new('S-1-5-32-559')))){throw 'ETW requires existing administrator or Performance Log Users permission'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $output -Operation Capture -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$session='FvrStockPresent-'+[Guid]::NewGuid().ToString('N')
$before=Sessions
if(HasSession $before){throw 'Unique ETW session already exists'}
[IO.Directory]::CreateDirectory($output)|Out-Null
Save 'storage.json' $budget
Save 'sessions-before.json' @{utc=[DateTime]::UtcNow.ToString('o');sessions=$before}
$arguments=@('--process_id',[string]$identity.pid,'--output_file','presents.csv','--session_name',$session,
 '--timed','10','--terminate_after_timed','--terminate_on_proc_exit','--no_console_stats',
 '--no_track_gpu','--no_track_input','--track_etw_status','--date_time')
Save 'invocation.json' @{schema='fvr.runtime03.present-check.v1';original_game=$identity;tool_sha256=$expectedTool;session=$session;arguments=$arguments;
 utc=[DateTime]::UtcNow.ToString('o');monitored_output_threshold_bytes=8MB;storage_reservation_bytes=16MB;watchdog_seconds=25;provider_filter='event IDs, not PID';output_filter='original PID';
 placement_sha256=(Sha (Join-Path $scene 'original-graphics-placement.json'));window_before_sha256=(Sha (Join-Path $scene 'window-before-present.json'));
 etw_memory_pool_max_bytes=64MB;etl_written=$false;input_sent=$false;native_attached=$false;user_window_required=$true}
$capture=$null;$stop=$null;$failure=$null;$cleanupFailures=[Collections.Generic.List[string]]::new()
$sameLifetime=$false;$timedOut=$false;$capExceeded=$false;$sessionAbsent=$false
try{
 Lease;$null=OriginalGame
 $capture=StartTool $arguments 'capture'
 $captureHandle=$capture.Handle
 $timer=[Diagnostics.Stopwatch]::StartNew()
 while(-not $capture.WaitForExit(250)){
  $bytes=(@(Get-ChildItem -LiteralPath $output -File)|Measure-Object Length -Sum).Sum
  if($bytes -gt 8MB){$capExceeded=$true;throw 'Present output exceeded bounded allocation'}
  if($timer.Elapsed.TotalSeconds -gt 25){$timedOut=$true;throw 'Present collector exceeded watchdog'}
 }
 if($capture.ExitCode -ne 0){throw ('Present collector failed: '+$capture.ExitCode)}
 $null=OriginalGame;$sameLifetime=$true
}catch{$failure=$_.Exception.Message}
finally{
 # Retire the collector before the final query: it must no longer be able to
 # create a session after an absence observation. Only owned process objects
 # and the unique session name can be stopped.
 try{
  if($capture -and -not $capture.HasExited){
   $capture.Kill()
   if(-not $capture.WaitForExit(2000)){throw 'Owned collector remains after bounded retirement'}
  }
 }catch{$cleanupFailures.Add($_.Exception.Message)}
 try{
  if($capture -and -not $capture.HasExited){throw 'Collector retirement unconfirmed; session absence cannot be established'}
  $after=Sessions
  if(HasSession $after){
   $stop=StartTool @('--session_name',$session,'--terminate_existing_session') 'owned-session-stop'
   if(-not $stop.WaitForExit(3000)){
    $stop.Kill()
    if(-not $stop.WaitForExit(2000)){throw 'Owned session-stop process remains'}
    throw 'Owned ETW stop timed out; absence is unconfirmed'
   }
   $after=Sessions
  }
  if(($capture -and -not $capture.HasExited) -or (HasSession $after)){throw 'Collector/session remains; cleanup is unconfirmed'}
  $sessionAbsent=$true
  Save 'sessions-after.json' @{utc=[DateTime]::UtcNow.ToString('o');session=$session;owned_session_absent=$true;owned_collector_exited=$true;sessions=$after}
 }catch{$cleanupFailures.Add($_.Exception.Message)}
 try{
  $finalBytes=(@(Get-ChildItem -LiteralPath $output -File)|Measure-Object Length -Sum).Sum
  if($finalBytes -gt 8MB){$capExceeded=$true;if(-not $failure){$failure='Present output exceeded monitored threshold on final flush'}}
 }catch{$cleanupFailures.Add($_.Exception.Message)}
 Save 'result.json' @{schema='fvr.runtime03.present-result.v1';utc=[DateTime]::UtcNow.ToString('o');failure=$failure;cleanup_failures=@($cleanupFailures);
  exit_code=$(if($capture -and $capture.HasExited){$capture.ExitCode}else{$null});same_game_lifetime=$sameLifetime;
  timed_out=$timedOut;output_cap_exceeded=$capExceeded;owned_collector_remaining=($capture -and -not $capture.HasExited);
  owned_stop_process_remaining=($stop -and -not $stop.HasExited);owned_session_absent=$sessionAbsent;
  presentation_accepted=$false;loss_counters_review_required=$true;physical_pixels_not_measured=$true}
}
if($cleanupFailures.Count){throw ($cleanupFailures -join '; ')}
if($failure){throw $failure}
Write-Output (Join-Path $output 'result.json')

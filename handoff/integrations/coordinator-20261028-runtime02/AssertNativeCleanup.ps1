#requires -Version 7.5
# Defines one read-only assertion. Its return value is immutable JSON text for
# the caller to preserve; this file never closes a process or writes evidence.
# StockModuleSnapshotPin pins {schema:'fvr.runtime02.stock-modules.v1',
# original_game:<scene process.json>, observed_utc:<UTC>,
# modules:[{ModuleName:<name>,FileName:<absolute path>},...]}, taken before RunBuilt.
function Assert-Runtime02NativeCleanup {
 [CmdletBinding()]
 param([Parameter(Mandatory)][Alias('SceneIdentity')]$ExactSceneIdentity,
       [Parameter(Mandatory)][string]$NativeRunDirectory,
       [Parameter(Mandatory)][string]$RuntimeBinding,
       [Parameter(Mandatory)]$StockModuleSnapshotPin)
 $ErrorActionPreference='Stop'
 Set-StrictMode -Version Latest
 $root='G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-runtime02\magazine'
 $buildPath=Join-Path $root 'build01/run01/build-receipt.json'
 $buildSha='273b0504dfa002ead9a5ee7b92edc2ff7867875cf84e95857cb255cbec17625c'
 $gamePath='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
 $gameSha='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
 $definition='2e1cb951041973ccac4bedfa77714cdc2d69220f487efdcfe05eb222d8c1960c'
 $observedPins=[Collections.Generic.List[object]]::new()

 function Value($Object,[string]$Name) {
  if($Object -is [Collections.IDictionary]) {
   if(-not $Object.Contains($Name)){throw "Missing cleanup field: $Name"}
   return ,$Object[$Name]
  }
  if($null -eq $Object -or -not $Object.PSObject.Properties[$Name]){throw "Missing cleanup field: $Name"}
  return ,$Object.PSObject.Properties[$Name].Value
 }
 function Integer($Number) {
  if($Number -isnot [int] -and $Number -isnot [long]){throw 'Cleanup integer is missing or has the wrong JSON type'}
  return [long]$Number
 }
 function Bool($Object,[string]$Name,[bool]$Expected) {
  $v=Value $Object $Name
  if($v -isnot [bool] -or $v -ne $Expected){throw "Cleanup boolean differs: $Name"}
 }
 function Text($Object,[string]$Name) {
  $v=Value $Object $Name
  if($v -isnot [string] -or [string]::IsNullOrWhiteSpace($v)){throw "Cleanup string is missing: $Name"}
  return $v
 }
 function Full([string]$Path) {
  if(-not [IO.Path]::IsPathFullyQualified($Path)){throw 'Cleanup paths must be absolute'}
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
 }
 function NoLinks([string]$Path) {
  $p=Full $Path
  while($p) {
   if(-not (Test-Path -LiteralPath $p)){throw "Cleanup path missing: $p"}
   if((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked cleanup path refused: $p"}
   $p=Split-Path -Parent $p
  }
 }
 function SamePath([string]$Actual,[string]$Expected) {
  if((Full $Actual) -ine (Full $Expected)){throw "Cleanup path differs: $Actual"}
 }
 function FilePin([string]$Path,[long]$Maximum=4MB) {
  $path=Full $Path;NoLinks $path;$item=Get-Item -LiteralPath $path
  if($item.PSIsContainer -or $item.Length -gt $Maximum){throw "Cleanup file exceeds its bound: $path"}
  $size=$item.Length;$written=$item.LastWriteTimeUtc
  $sha=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  $after=Get-Item -LiteralPath $path
  if($after.Length -ne $size -or $after.LastWriteTimeUtc -ne $written){throw "Cleanup file changed during hashing: $path"}
  return @{path=$path;bytes=$size;sha256=$sha}
 }
 function CheckPin($Pin,[string]$Expected,[long]$Maximum=4MB) {
  SamePath (Text $Pin 'path') $Expected
  $size=Integer (Value $Pin 'bytes');$sha=Text $Pin 'sha256'
  if($size -lt 0 -or $sha -cnotmatch '^[a-f0-9]{64}$'){throw 'Malformed cleanup file pin'}
  $actual=FilePin $Expected $Maximum
  if($actual.bytes -ne $size -or $actual.sha256 -cne $sha){throw "Cleanup file pin changed: $Expected"}
  $observedPins.Add($actual)
  return $actual
 }
 function Json([string]$Path,[long]$Maximum=64KB) {
  $pin=FilePin $Path $Maximum
  if($pin.bytes -eq 0){throw "Empty cleanup receipt: $Path"}
  $v=Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json -AsHashtable -DateKind String -Depth 64
  if($v -isnot [Collections.IDictionary]){throw "Cleanup receipt must be a JSON object: $Path"}
  $null=CheckPin $pin $Path $Maximum
  return $v
 }
 function Schema($Object,[string]$Expected) {
  if((Text $Object 'schema') -cne $Expected){throw "Cleanup schema differs: $Expected"}
 }
 function Utc($Object,[string]$Name) {
  $text=Text $Object $Name
  if($text -notmatch '(Z|[+-]00:00)$'){throw "Explicit UTC timestamp required: $Name"}
  return [DateTimeOffset]::Parse($text,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime
 }
 function Birth($Object) {
  $v=Value $Object 'creation_filetime'
  if($v -is [string]) {
   if($v -cnotmatch '^[1-9][0-9]{0,18}$'){throw 'Malformed original creation FILETIME'}
   $v=[long]::Parse($v,[Globalization.CultureInfo]::InvariantCulture)
  } else {$v=Integer $v}
  if($v -le 0){throw 'Invalid original creation FILETIME'}
  return [long]$v
 }
 $pidExpected=Integer (Value $ExactSceneIdentity 'pid');$birthExpected=Birth $ExactSceneIdentity
 if($pidExpected -le 0 -or $pidExpected -gt [int]::MaxValue){throw 'Invalid original game PID'}
 SamePath (Text $ExactSceneIdentity 'path') $gamePath
 Bool $ExactSceneIdentity 'root_launched' $true
 $gameFilePin=FilePin $gamePath 128MB
 if((Text $ExactSceneIdentity 'sha256') -cne $gameSha -or $gameFilePin.sha256 -cne $gameSha){throw 'Original game executable identity differs'}
 $observedPins.Add($gameFilePin)
 function GameIdentity($Object,[bool]$RequireStart=$true) {
  if((Integer (Value $Object 'pid')) -ne $pidExpected -or (Birth $Object) -ne $birthExpected){throw 'Cleanup belongs to another game lifetime'}
  SamePath (Text $Object 'path') $gamePath
  if($RequireStart -and (Utc $Object 'start_utc').ToFileTimeUtc() -ne $birthExpected){throw 'Original game start time differs'}
 }
 function GameStillOriginal($Game) {
  $Game.Refresh()
  if($Game.HasExited -or $Game.Id -ne $pidExpected -or $Game.Path -ine $gamePath -or $Game.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $birthExpected){throw 'Original live game lifetime changed; preserve game/profiles'}
  $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
  if($games.Count -ne 1 -or $games[0].Id -ne $pidExpected){throw 'Exactly one original game is required'}
 }
 function HelpersIdle {
  if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeMenuProbe,SceneMenuSidecar421,SceneMenuSidecar431,SceneMenuSidecarR46,ExceptionWatch422,ProvisionRunner -ErrorAction SilentlyContinue){throw 'Native/receiver/helper process still active; preserve game'}
 }

 $native=Full $NativeRunDirectory;NoLinks $native
 if((Split-Path -Parent $native) -ine $root -or (Split-Path -Leaf $native) -cnotmatch '^run[0-9]{2}$' -or -not (Get-Item -LiteralPath $native).PSIsContainer){throw 'Exact runtime02 magazine/runNN directory required'}
 $selected=Get-Item -LiteralPath $native
 function LatestAttempt {
  foreach($attempt in @(Get-ChildItem -LiteralPath $root -Directory -Force|Where-Object {$_.Name -cmatch '^run[0-9]{2}$'})) {
   NoLinks $attempt.FullName
   # RunBuilt creates a new directory before admission receipts. A newer partial
   # directory must not be skipped in favor of an older passing retirement.
   if($attempt.FullName -ine $native -and ($attempt.Name -cgt $selected.Name -or $attempt.CreationTimeUtc -gt $selected.CreationTimeUtc)){throw 'A newer magazine attempt exists, possibly incomplete; no stale cleanup fallback'}
  }
 }
 LatestAttempt
 SamePath $RuntimeBinding (Join-Path $root 'runtime-binding.json')
 $runtimeBindingPin=FilePin $RuntimeBinding 64KB
 $bound=Json $RuntimeBinding;Schema $bound 'fvr.runtime02.magazine.runtime-binding.v1'
 $null=CheckPin $runtimeBindingPin $RuntimeBinding 64KB
 $buildPin=CheckPin (Value $bound 'build') $buildPath
 if($buildPin.sha256 -cne $buildSha){throw 'Only the reviewed completed diagnostic build is admitted'}
 $build=Json $buildPath 4MB;Schema $build 'fvr.r56.generic-single-magazine.build.v1'
 foreach($field in 'diagnostic_only','flags_only','campaign_magazine','single_magazine','normal_outputs_exactly_restored','normal_configuration_restored'){Bool $build $field $true}
 $null=CheckPin @{path=(Join-Path $native 'build-receipt.json');bytes=$buildPin.bytes;sha256=$buildPin.sha256} (Join-Path $native 'build-receipt.json')
 function MatchPin($Actual,$Expected,[long]$Maximum=4MB) {
  $checked=CheckPin $Actual (Text $Expected 'path') $Maximum
  if($checked.bytes -ne (Integer (Value $Expected 'bytes')) -or $checked.sha256 -cne (Text $Expected 'sha256')){throw 'Cleanup pins belong to different artifacts'}
 }
 MatchPin (Value $bound 'source') (Value $build 'source_preparation')
 MatchPin (Value $bound 'workflow') (Value $build 'workflow')
 $null=CheckPin (Value $bound 'admission') (Join-Path $root 'admission-inputs.json')
 $recipe=Split-Path (Text (Value $bound 'workflow') 'path')
 $reviewPath=Join-Path $recipe 'operation-header-review.json'
 $operationReviewPin=CheckPin (Value $bound 'operation_header_review') $reviewPath 64KB
 if($operationReviewPin.sha256 -cne '64a400f123a427eb52b97865b9fb5bc9ff555dd3288f007a249c72651cb92ec6'){throw 'Exact successful operation-header review required'}
 $operationReview=Json $reviewPath
 Schema $operationReview 'fvr.runtime02.magazine.operation-header-review.v1'
 MatchPin (Value $operationReview 'build') $buildPin
 MatchPin (Value $operationReview 'original_workflow') (Value $bound 'workflow')
 $readyPath=Join-Path $recipe 'RunReady.ps1'
 $readyPins=@((Value $operationReview 'successors')|Where-Object {(Text $_ 'path') -ieq $readyPath})
 if($readyPins.Count -ne 1){throw 'Reviewed RunReady recipe pin required'}
 $readyPin=CheckPin $readyPins[0] $readyPath
 $metadata=Json (Join-Path $native 'metadata-admission.json') 1MB
 $documentReviewPin=$null
 if($metadata.Contains('supplemental_document_review')){
  $documentReviewPin=CheckPin $metadata.supplemental_document_review (Join-Path $recipe 'metadata-review.json') 64KB
  if($documentReviewPin.sha256 -cne '2b8f1197328cfdeacfa9a6d1167ce6e0f375d6d7a8b1681eade02b74e2e9ab0a'){throw 'Exact documentary successor review required'}
  $documentReview=Json $documentReviewPin.path
  Schema $documentReview 'fvr.runtime02.magazine.document-review.v1'
  Bool $documentReview 'documentary_only' $true;Bool $documentReview 'native_admission_granted' $false
  $documentPins=Value $documentReview 'pins'
  MatchPin (Value $documentPins 'build') $buildPin
  MatchPin (Value $documentPins 'runtime_binding') $runtimeBindingPin
  MatchPin (Value $documentPins 'original_runner') $readyPin
  $null=CheckPin (Value $documentPins 'reviewed_admission') (Join-Path $recipe 'AdmissionReviewed.py')
  $readyPin=CheckPin (Value $documentPins 'reviewed_runner') (Join-Path $recipe 'RunMetadata.ps1')
  Bool $metadata 'original_plan_preserved' $true
 }
 $monitorPath=Join-Path $native 'monitor-script.ps1'
 $null=CheckPin @{path=$monitorPath;bytes=$readyPin.bytes;sha256=$readyPin.sha256} $monitorPath
 $binaries=Value $build 'binaries';$probe=Value $binaries 'probe';$trace=Value $binaries 'trace';$receiver=Value $binaries 'receiver'
 $null=CheckPin $probe (Join-Path (Split-Path $buildPath) 'BC2NativeProbe.dll') 128MB
 $null=CheckPin $trace (Join-Path (Split-Path $buildPath) 'BC2NativeTrace.exe') 128MB
 $null=CheckPin $receiver (Text $receiver 'path') 128MB
 $stockPin=CheckPin $StockModuleSnapshotPin (Text $StockModuleSnapshotPin 'path') 1MB
 $stock=Json $stockPin.path 1MB;Schema $stock 'fvr.runtime02.stock-modules.v1'
 GameIdentity (Value $stock 'original_game') $false
 $stockUtc=Utc $stock 'observed_utc'
 if($stockUtc.ToFileTimeUtc() -lt $birthExpected){throw 'Stock module snapshot predates the original game'}
 $stockModules=Value $stock 'modules'
 if($stockModules -isnot [array] -or $stockModules.Count -eq 0 -or $stockModules.Count -gt 1024){throw 'Bounded stock module array required'}
 $stockSet=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($module in $stockModules) {
  $name=Text $module 'ModuleName';$path=Full (Text $module 'FileName')
  if([IO.Path]::GetFileName($path) -ine $name -or $stockSet.ContainsKey($path)){throw 'Duplicate or inconsistent stock module row'}
  if($name -like 'BC2*.dll' -or $name -like '*NativeProbe*' -or $name -like '*TestRange*' -or $name -like '*PreWorldMsaa*'){throw 'Stock snapshot contains a diagnostic/provision module'}
  $stockSet.Add($path,$name)
 }
 if(-not $stockSet.ContainsKey($gamePath)){throw 'Stock module snapshot lacks the original game image'}

 Schema $metadata 'fvr.r55.single-magazine.metadata-admission.v1'
 Bool $metadata 'metadata_admitted' $true;Bool $metadata 'runtime_admission_granted' $false
 $setup=Json (Join-Path $native 'setup-before.json')
 Schema $setup 'fvr.r55.single_magazine_setup.v1'
 if((Integer (Value $setup 'pid')) -ne $pidExpected -or (Utc $setup 'start_utc').ToFileTimeUtc() -ne $birthExpected -or (Text $setup 'expected_asset') -cne 'Nambu100' -or (Text $setup 'definition') -cne $definition){throw 'Setup does not admit this original Nambu game lifetime'}
 Bool $setup 'preflight_passed' $true;Bool $setup 'on_foot_required' $true;Bool $setup 'paired_weapon_required' $false
 $preflightPath=Join-Path $native 'inventory-setup-before.json'
 SamePath (Text $setup 'preflight_path') $preflightPath
 $preflightPin=FilePin $preflightPath 4MB
 if($preflightPin.sha256 -cne (Text $setup 'preflight_sha256')){throw 'Original setup preflight changed'}
 $observedPins.Add($preflightPin)
 $invocationPath=Join-Path $native 'trace-invocation.json';$resultPath=Join-Path $native 'trace-result.json'
 $cleanupPath=Join-Path $native 'receiver-cleanup.json';$failurePath=Join-Path $native 'trace-failure.json'
 foreach($name in 'trace-invocation.json','trace-result.json','trace-failure.json') {
  if(Test-Path -LiteralPath (Join-Path $native ($name+'.pending'))){throw 'Unpublished lifecycle evidence remains; preserve the incomplete attempt'}
 }
 $invocation=Json $invocationPath;$result=Json $resultPath;$cleanup=Json $cleanupPath
 Schema $invocation 'fvr.runtime02.magazine.trace-invocation.v1'
 Schema $result 'fvr.runtime02.magazine.trace-result.v1'
 Schema $cleanup 'fvr.bc2.boat_receiver_cleanup.v1'
 $invocationPin=FilePin $invocationPath 64KB;$resultPin=FilePin $resultPath 64KB
 function TraceCommon($Record) {
  GameIdentity (Value $Record 'original_game')
  MatchPin (Value $Record 'build_receipt') $buildPin
  MatchPin (Value $Record 'executable') $trace 128MB
  MatchPin (Value $Record 'probe') $probe 128MB
  SamePath (Text $Record 'receiver_cleanup_path') $cleanupPath
  Bool $Record 'forced_termination' $false
  # These records describe observation only; native retirement is checked below.
  Bool $Record 'remote_native_retirement_proven' $false
 }
 foreach($record in @($invocation,$result)) {
  TraceCommon $record
  Bool (Value $record 'original_game') 'original_handle_retained' $true
  SamePath (Text $record 'later_failure_path') $failurePath
  if($null -ne (Value $record 'trace_process_identity') -or (Text $record 'trace_process_identity_status') -cne 'not_exposed_by_call_operator'){throw 'Unexpected CLI process identity claim'}
 }
 MatchPin (Value $result 'invocation') $invocationPin 64KB
 $invokedUtc=Utc $invocation 'recorded_utc';$returnedUtc=Utc $result 'observation_utc'
 if((Utc $result 'invocation_utc') -ne $invokedUtc -or $stockUtc -ge $invokedUtc -or $returnedUtc -lt $invokedUtc -or $returnedUtc -gt [DateTime]::UtcNow){throw 'Lifecycle/snapshot timestamp ordering differs'}
 if((Integer (Value $invocation 'requested_native_seconds')) -ne 60 -or (Text $invocation 'call_method') -cne 'synchronous_powershell_call_operator'){throw 'Unexpected native invocation method'}
 $channelPath=Join-Path $native 'receiver/channel.txt';$channelPin=FilePin $channelPath 4KB
 $channel=([IO.File]::ReadAllText($channelPath)).Trim()
 if($channel -cnotmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid original receiver channel'}
 $observedPins.Add($channelPin)
 $expectedArgs=@('--game',(Split-Path $gamePath),'--dll',$probe.path,'--report',(Join-Path $native 'native-trace.json'),'--stream',$channel,'--seconds','60','--ordinary-resource-input-probe')
 foreach($record in @($invocation,$result)) {
  $traceArgs=Value $record 'arguments'
  if($traceArgs -isnot [array] -or $traceArgs.Count -ne $expectedArgs.Count){throw 'Trace argument count differs'}
  for($i=0;$i -lt $traceArgs.Count;$i++){if($traceArgs[$i] -isnot [string] -or $traceArgs[$i] -cne $expectedArgs[$i]){throw 'Trace argument differs from the bound finite diagnostic'}}
  $logs=Value $record 'logs'
  SamePath (Text $logs 'stdout') (Join-Path $native 'bootstrap.json')
  SamePath (Text $logs 'stderr') (Join-Path $native 'bootstrap-error.txt')
  SamePath (Text $logs 'native_report') (Join-Path $native 'native-trace.json')
 }
 foreach($field in 'trace_attempted','trace_returned','cli_exit_observed'){Bool $result $field $true}
 $cliExit=Integer (Value $result 'trace_exit')
 if($null -ne (Value $result 'invocation_exception')){throw 'Invocation exception prevents complete CLI observation'}
 Bool $result 'native_report_parsed' $false
 if((Integer (Value $result 'elapsed_ms')) -lt 0){throw 'Invalid CLI elapsed time'}
 $gameAfter=Value $result 'retained_game_after';Bool $gameAfter 'has_exited' $false
 if((Text $gameAfter 'observation') -cne 'original_process_alive'){throw 'Original game was not observed alive after the call'}
 $available=Value $result 'available_files'
 foreach($spec in @(@('bootstrap','bootstrap.json',4MB),@('stderr','bootstrap-error.txt',4MB),@('native_report','native-trace.json',96MB))) {
  $e=Value $available $spec[0];Bool $e 'exists' $true
  if((Text $e 'status') -cne 'hashed_at_observation' -or $null -ne (Value $e 'error')){throw 'Original trace file was not stably observed'}
  $null=CheckPin $e (Join-Path $native $spec[1]) $spec[2]
 }
 $bootstrap=Json (Join-Path $native 'bootstrap.json') 4MB
 Schema $bootstrap 'bc2vr.native-run-outcome.v1'
 if((Integer (Value $bootstrap 'pid')) -ne $pidExpected -or (Text $bootstrap 'outcome') -cne 'remote_thread_exited' -or (Integer (Value $bootstrap 'probe_exit')) -ne 0){throw 'Remote native thread retirement is not proven'}
 Bool $bootstrap 'remote_thread_exit_observed' $true;Bool $bootstrap 'report_nonempty' $true
 $nativeReport=Json (Join-Path $native 'native-trace.json') 96MB
 if((Integer (Value $nativeReport 'pid')) -ne $pidExpected){throw 'Native report belongs to another process'}
 Bool $nativeReport 'hooks_disabled' $true
 $flow=Value (Value $nativeReport 'gameplay') 'reload_flow';Bool $flow 'drained' $true
 if((Integer (Value $flow 'in_flight')) -ne 0){throw 'Native operations remain in flight'}
 foreach($field in 'native_cleanup_confirmed','receiver_started','receiver_exited'){Bool $cleanup $field $true}
 Bool $cleanup 'forced_termination' $false
 if((Text $cleanup 'observation') -cne 'retained_process_exited' -or (Integer (Value $cleanup 'budget_ms')) -ne 85000){throw 'Original receiver exit is not proven'}
 $receiverPid=Integer (Value $cleanup 'pid');$receiverExit=Integer (Value $cleanup 'receiver_exit');$receiverStart=Utc $cleanup 'start_utc'
 if($receiverPid -le 0 -or $receiverPid -gt [int]::MaxValue -or $receiverPid -eq $pidExpected -or $receiverStart -lt $stockUtc -or $receiverStart -gt $invokedUtc){throw 'Receiver lifetime does not belong to this attachment'}
 MatchPin (Value $cleanup 'trace_invocation') $invocationPin 64KB
 MatchPin (Value $cleanup 'trace_result') $resultPin 64KB
 SamePath (Text $cleanup 'trace_failure_path') $failurePath
 # receiver_identity_verified is a live-process fallback flag. The successful
 # retained Process.HasExited branch deliberately leaves it false.
 Bool $cleanup 'receiver_identity_verified' $false
 $failure=$null
 if(Test-Path -LiteralPath $failurePath) {
  $failure=Json $failurePath;Schema $failure 'fvr.runtime02.magazine.trace-failure.v1';TraceCommon $failure
  MatchPin (Value $failure 'invocation') $invocationPin 64KB
  MatchPin (Value $failure 'result') $resultPin 64KB
  $null=Text $failure 'failure_stage';$null=Text $failure 'failure_message'
  if((Utc $failure 'observed_utc') -lt $invokedUtc){throw 'Failure record predates its invocation'}
 }
 HelpersIdle
 $game=Get-Process -Id ([int]$pidExpected) -ErrorAction SilentlyContinue
 $originalAbsent=$null -eq $game
 if($originalAbsent -and (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){throw 'Another game owns the profiles; absent original cannot authorize cleanup'}
 try {
  function ModuleSet {
   GameStillOriginal $game
   $seen=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
   $probeCount=0
   foreach($module in @($game.Modules)) {
    $path=Full $module.FileName;$name=$module.ModuleName
    if($seen.ContainsKey($path)){throw 'Duplicate live module path'}
    $seen.Add($path,$name)
    if($path -ieq $probe.path -and $name -ieq 'BC2NativeProbe.dll'){$probeCount++;continue}
    if(-not $stockSet.ContainsKey($path) -or $stockSet[$path] -ine $name){throw "Unknown additional module; preserve game: $name"}
   }
   if($probeCount -ne 1){throw 'Exactly one bound diagnostic probe module must remain loaded'}
   return @($seen.Keys|Sort-Object)
  }
  $modulesBefore=$null
  if($game){$null=$game.Handle;GameStillOriginal $game;$modulesBefore=@(ModuleSet)}
  # Recheck every observed receipt/artifact before returning a snapshot binding.
  foreach($pin in @($observedPins.ToArray())) {
   $fresh=FilePin $pin.path ([Math]::Max([long]$pin.bytes,1))
   if($fresh.bytes -ne $pin.bytes -or $fresh.sha256 -cne $pin.sha256){throw 'Cleanup evidence changed before binding'}
  }
  LatestAttempt;HelpersIdle
  if($game) {
   GameStillOriginal $game
   if((@(ModuleSet)-join "`n") -cne ($modulesBefore-join "`n")){throw 'Live module set changed during cleanup validation'}
  } else {
   if((Get-Process -Id ([int]$pidExpected) -ErrorAction SilentlyContinue) -or (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){throw 'A process reacquired the original PID or game profiles during absent-game validation'}
  }
  $pins=@($observedPins.ToArray()|Sort-Object path -Unique)
  return (@{schema='fvr.runtime02.native-cleanup-binding.v1';observed_utc=[DateTime]::UtcNow.ToString('o');read_only=$true
    native_run=$native;original_game=@{pid=$pidExpected;path=$gamePath;creation_filetime=[string]$birthExpected}
    runtime_binding=$runtimeBindingPin;operation_header_review=$operationReviewPin;supplemental_document_review=$documentReviewPin;stock_modules=$stockPin;evidence=$pins
    diagnostic_probe=$probe;original_process_absent=$originalAbsent;live_modules_verified=(-not $originalAbsent);module_paths=$modulesBefore
    remote_thread_exited=$true;hooks_disabled=$true;reload_flow_drained=$true;in_flight=0
    cli_exit=$cliExit;cli_process_identity='not_exposed_by_call_operator';receiver=@{pid=$receiverPid;start_utc=$receiverStart.ToString('o');exit_code=$receiverExit;observation='retained_process_exited'}
    trace_result_failure=@{stage=(Value $result 'failure_stage');message=(Value $result 'failure_message')};trace_failure=$failure
    native_cleanup_confirmed=$true;overall_test_pass_required_for_cleanup=$false;native_reload_accepted=$false;headset_verified=$false
    game_close_performed=$false;normal_exit_observed=$false;profiles_restored=$false;forced_termination=$false
    limitation='Snapshot only: caller must recheck original lifetime, lease and helper absence before close/restoration. An absent original permits no live module join and proves neither normal exit nor native acceptance.'
   }|ConvertTo-Json -Depth 24)
 } finally {if($game){$game.Dispose()}}
}

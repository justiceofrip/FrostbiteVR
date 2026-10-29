#requires -Version 7.5
<#
Offline synthetic regression fixture. Root executes this explicitly.
No process enumeration reaches Windows: only the local Get-Process mock below
is visible to the assertion. Tiny text files stand in for binaries; no game,
Win32, launch, close, build, Git, native acceptance or headset claim is involved.
Only five fixture constants change in the exact pinned production helper:
evidence root, game path, and build/game/operation-review hashes. No predicate
is removed or replaced. Fixtures and failures remain on G: for inspection.
#>
[CmdletBinding()]
param([string]$Output=(Join-Path 'G:\Unity\VRMMO\FrostbiteVR\build\scratch' ('runtime02-native-cleanup-'+[Guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
$fixtureParent='G:\Unity\VRMMO\FrostbiteVR\build\scratch'
$Output=[IO.Path]::GetFullPath($Output)
if((Split-Path $Output -Parent) -ine $fixtureParent -or (Split-Path $Output -Leaf) -cnotmatch '^runtime02-native-cleanup-[a-f0-9]{32}$'){throw 'Use a fresh bounded G: cleanup fixture directory'}
if(Test-Path -LiteralPath $Output){throw 'Preserve existing fixture evidence'}
$helper=Join-Path $PSScriptRoot 'AssertNativeCleanup.ps1'
$helperSha='3869cf17692e5b4c1099fd18f842d5cb0822bd42d12718cb94fcd2f25548db22'
if((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant() -cne $helperSha){throw 'Review and rebind the changed cleanup helper before running this fixture'}
$helperSource=[IO.File]::ReadAllText($helper)
[IO.Directory]::CreateDirectory($Output)|Out-Null

function Need($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Fixture-Pin([string]$Path){@{path=[IO.Path]::GetFullPath($Path);bytes=(Get-Item -LiteralPath $Path).Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}}
function Fixture-Text([string]$Path,[string]$Text){
 if(Test-Path -LiteralPath $Path){throw 'Fixture never overwrites an existing receipt'}
 [IO.Directory]::CreateDirectory((Split-Path $Path -Parent))|Out-Null
 [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Fixture-Json([string]$Path,$Value){Fixture-Text $Path ($Value|ConvertTo-Json -Depth 32)}
function Observed-File([string]$Path){$p=Fixture-Pin $Path;$p.exists=$true;$p.status='hashed_at_observation';$p.error=$null;return $p}

function New-CleanupFixture([string]$Name,[string]$Fault){
 $case=Join-Path $Output $Name;$root=Join-Path $case 'magazine';$run=Join-Path $root 'run01'
 $compiled=Join-Path $root 'build01/run01';$recipe=Join-Path $case 'recipe'
 $gamePath=Join-Path $case 'game/BFBC2Game.exe'
 Fixture-Text $gamePath 'SYNTHETIC NONEXECUTABLE GAME FIXTURE'
 foreach($name in 'BC2NativeProbe.dll','BC2NativeTrace.exe','BC2NativeIpcProbe.exe'){Fixture-Text (Join-Path $compiled $name) ('SYNTHETIC NONEXECUTABLE '+$name)}
 Fixture-Json (Join-Path $recipe 'source-preparation.json') @{synthetic=$true;files=@()}
 Fixture-Json (Join-Path $recipe 'workflow-preparation.json') @{synthetic=$true;scope='offline cleanup fixture'}
 Fixture-Text (Join-Path $recipe 'RunReady.ps1') '# Synthetic monitor identity only; never executed.'
 $probe=Fixture-Pin (Join-Path $compiled 'BC2NativeProbe.dll')
 $trace=Fixture-Pin (Join-Path $compiled 'BC2NativeTrace.exe')
 $receiver=Fixture-Pin (Join-Path $compiled 'BC2NativeIpcProbe.exe')
 $source=Fixture-Pin (Join-Path $recipe 'source-preparation.json');$workflow=Fixture-Pin (Join-Path $recipe 'workflow-preparation.json')
 $ready=Fixture-Pin (Join-Path $recipe 'RunReady.ps1')
 $build=@{schema='fvr.r56.generic-single-magazine.build.v1';synthetic=$true;diagnostic_only=$true;flags_only=$true;campaign_magazine=$true;single_magazine=$true;normal_outputs_exactly_restored=$true;normal_configuration_restored=$true;source_preparation=$source;workflow=$workflow;binaries=@{probe=$probe;trace=$trace;receiver=$receiver}}
 $buildPath=Join-Path $compiled 'build-receipt.json';Fixture-Json $buildPath $build;$buildPin=Fixture-Pin $buildPath
 Fixture-Json (Join-Path $recipe 'operation-header-review.json') @{schema='fvr.runtime02.magazine.operation-header-review.v1';synthetic=$true;build=$buildPin;original_workflow=$workflow;successors=@($ready)}
 $review=Fixture-Pin (Join-Path $recipe 'operation-header-review.json')
 Fixture-Json (Join-Path $root 'admission-inputs.json') @{synthetic=$true}
 $bindingPath=Join-Path $root 'runtime-binding.json'
 Fixture-Json $bindingPath @{schema='fvr.runtime02.magazine.runtime-binding.v1';synthetic=$true;build=$buildPin;source=$source;workflow=$workflow;admission=(Fixture-Pin (Join-Path $root 'admission-inputs.json'));operation_header_review=$review}
 Fixture-Text (Join-Path $run 'build-receipt.json') ([IO.File]::ReadAllText($buildPath))
 Fixture-Text (Join-Path $run 'monitor-script.ps1') ([IO.File]::ReadAllText($ready.path))
 $start=[DateTime]::Parse('2020-01-01T00:00:00Z').ToUniversalTime();$gamePid=4242
 $identity=@{pid=$gamePid;path=$gamePath;creation_filetime=[string]$start.ToFileTimeUtc();sha256=(Fixture-Pin $gamePath).sha256;root_launched=$true}
 $stockModules=@(@{ModuleName='BFBC2Game.exe';FileName=$gamePath})
 $stockPath=Join-Path $case 'stock-modules.json'
 Fixture-Json $stockPath @{schema='fvr.runtime02.stock-modules.v1';original_game=$identity;observed_utc=$start.AddMinutes(1).ToString('o');modules=$stockModules}
 Fixture-Json (Join-Path $run 'metadata-admission.json') @{schema='fvr.r55.single-magazine.metadata-admission.v1';metadata_admitted=$true;runtime_admission_granted=$false;synthetic=$true}
 $preflightPath=Join-Path $run 'inventory-setup-before.json';Fixture-Json $preflightPath @{synthetic=$true;passed=$true}
 Fixture-Json (Join-Path $run 'setup-before.json') @{schema='fvr.r55.single_magazine_setup.v1';pid=$gamePid;start_utc=$start.ToString('o');expected_asset='Nambu100';definition='2e1cb951041973ccac4bedfa77714cdc2d69220f487efdcfe05eb222d8c1960c';preflight_passed=$true;on_foot_required=$true;paired_weapon_required=$false;preflight_path=$preflightPath;preflight_sha256=(Fixture-Pin $preflightPath).sha256}
 $logs=@{stdout=(Join-Path $run 'bootstrap.json');stderr=(Join-Path $run 'bootstrap-error.txt');native_report=(Join-Path $run 'native-trace.json')}
 Fixture-Json $logs.stdout @{schema='bc2vr.native-run-outcome.v1';pid=$gamePid;outcome='remote_thread_exited';probe_exit=0;remote_thread_exit_observed=$true;report_nonempty=$true;synthetic=$true}
 Fixture-Text $logs.stderr ''
 Fixture-Json $logs.native_report @{pid=$gamePid;hooks_disabled=($Fault -ne 'hooks_enabled');gameplay=@{reload_flow=@{drained=($Fault -ne 'undrained');in_flight=0}};synthetic=$true}
 $channel='synthetic_cleanup_channel';Fixture-Text (Join-Path $run 'receiver/channel.txt') $channel
 $traceArgs=@('--game',(Split-Path $gamePath),'--dll',$probe.path,'--report',$logs.native_report,'--stream',$channel,'--seconds','60','--ordinary-resource-input-probe')
 $traceIdentity=$identity.Clone();$traceIdentity.start_utc=$start.ToString('o');$traceIdentity.original_handle_retained=$true
 $cleanupPath=Join-Path $run 'receiver-cleanup.json';$failurePath=Join-Path $run 'trace-failure.json'
 $invocationPath=Join-Path $run 'trace-invocation.json';$resultPath=Join-Path $run 'trace-result.json'
 $invocation=@{schema='fvr.runtime02.magazine.trace-invocation.v1';original_game=$traceIdentity;build_receipt=$buildPin;executable=$trace;probe=$probe;receiver_cleanup_path=$cleanupPath;later_failure_path=$failurePath;forced_termination=$false;remote_native_retirement_proven=$false;trace_process_identity=$null;trace_process_identity_status='not_exposed_by_call_operator';recorded_utc=$start.AddMinutes(3).ToString('o');requested_native_seconds=60;call_method='synchronous_powershell_call_operator';arguments=$traceArgs;logs=$logs}
 Fixture-Json $invocationPath $invocation;$invocationPin=Fixture-Pin $invocationPath
 $resultIdentity=$traceIdentity.Clone()
 if($Fault -eq 'wrong_lifetime'){$resultIdentity.creation_filetime=[string]$start.AddSeconds(1).ToFileTimeUtc()}
 $cliExit=0
 $result=@{schema='fvr.runtime02.magazine.trace-result.v1';original_game=$resultIdentity;build_receipt=$buildPin;executable=$trace;probe=$probe;receiver_cleanup_path=$cleanupPath;later_failure_path=$failurePath;forced_termination=$false;remote_native_retirement_proven=$false;trace_process_identity=$null;trace_process_identity_status='not_exposed_by_call_operator';invocation=$invocationPin;invocation_utc=$start.AddMinutes(3).ToString('o');observation_utc=$start.AddMinutes(4).ToString('o');arguments=$traceArgs;logs=$logs;trace_attempted=$true;trace_returned=$true;cli_exit_observed=$true;trace_exit=$cliExit;invocation_exception=$null;native_report_parsed=$false;elapsed_ms=60000;retained_game_after=@{has_exited=$false;observation='original_process_alive'};available_files=@{bootstrap=(Observed-File $logs.stdout);stderr=(Observed-File $logs.stderr);native_report=(Observed-File $logs.native_report)};failure_stage=$(if($cliExit){'trace_cli_exit'}else{$null});failure_message=$(if($cliExit){'Synthetic nonzero CLI outcome with separately proven retirement'}else{$null})}
 # Withhold the published result for this negative case while retaining its bytes.
 $savedResult=if($Fault -eq 'missing_result'){Join-Path $run 'trace-result.withheld.json'}else{$resultPath}
 Fixture-Json $savedResult $result;$resultPin=Fixture-Pin $savedResult;$resultPin.path=$resultPath
 Fixture-Json $cleanupPath @{schema='fvr.bc2.boat_receiver_cleanup.v1';native_cleanup_confirmed=$true;receiver_started=$true;receiver_exited=$true;forced_termination=$false;observation='retained_process_exited';budget_ms=85000;pid=4343;receiver_exit=0;start_utc=$start.AddMinutes(2).ToString('o');trace_invocation=$invocationPin;trace_result=$resultPin;trace_failure_path=$failurePath;receiver_identity_verified=$false}
 if($Fault -eq 'changed_pin'){[IO.File]::AppendAllText($probe.path,' DELIBERATELY CHANGED AFTER PINNING',[Text.UTF8Encoding]::new($false))}
 $adaptations=@(
  @{before="`$root='G:\Unity\VRMMO\FrostbiteVR\reports\coordinator-20261028-runtime02\magazine'";after="`$root='$root'"},
  @{before="`$buildSha='273b0504dfa002ead9a5ee7b92edc2ff7867875cf84e95857cb255cbec17625c'";after="`$buildSha='$($buildPin.sha256)'"},
  @{before="`$gamePath='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'";after="`$gamePath='$gamePath'"},
  @{before="`$gameSha='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'";after="`$gameSha='$($identity.sha256)'"},
  @{before="'64a400f123a427eb52b97865b9fb5bc9ff555dd3288f007a249c72651cb92ec6'";after="'$($review.sha256)'"}
 )
 $adapted=$helperSource
 foreach($change in $adaptations){Need ([regex]::Matches($adapted,[regex]::Escape($change.before)).Count -eq 1) 'Expected exactly one original fixture constant';$adapted=$adapted.Replace($change.before,$change.after)}
 $reversed=$adapted
 for($i=$adaptations.Count-1;$i -ge 0;$i--){$reversed=$reversed.Replace($adaptations[$i].after,$adaptations[$i].before)}
 Need ($reversed -ceq $helperSource) 'Fixture changed a validator predicate'
 Fixture-Text (Join-Path $case 'AssertNativeCleanup.synthetic.ps1') $adapted
 Fixture-Json (Join-Path $case 'fixture.json') @{synthetic=$true;original_helper=(Fixture-Pin $helper);only_substitutions=$adaptations;fault=$Fault;no_native_acceptance=$true}
 return @{case=$case;root=$root;run=$run;binding=$bindingPath;identity=$identity;stock=(Fixture-Pin $stockPath);start=$start;probe=$probe;adapted=$adapted;cli_exit=$cliExit}
}

function Invoke-SyntheticAssertion($Fixture,[bool]$Absent){
 $script:fixtureMockCalls=0;$script:fixtureAbsent=$Absent
 $fake=[pscustomobject]@{Id=4242;Path=$Fixture.identity.path;StartTime=$Fixture.start;HasExited=$false;Handle=[IntPtr]42;RefreshCalls=0;DisposeCalls=0;Modules=@([pscustomobject]@{ModuleName='BFBC2Game.exe';FileName=$Fixture.identity.path},[pscustomobject]@{ModuleName='BC2NativeProbe.dll';FileName=$Fixture.probe.path})}
 $fake|Add-Member ScriptMethod Refresh { $this.RefreshCalls=$this.RefreshCalls+1 }
 $fake|Add-Member ScriptMethod Dispose { $this.DisposeCalls=$this.DisposeCalls+1 }
 $fake|Add-Member ScriptMethod CloseMainWindow { throw 'Offline assertion must never close a process' }
 $fake|Add-Member ScriptMethod Kill { throw 'Offline assertion must never kill a process' }
 $script:fixtureFakeGame=$fake
 function Get-Process {
  [CmdletBinding()]
  param([Parameter(Position=0)][string[]]$Name,[int]$Id)
  $script:fixtureMockCalls++
  if($PSBoundParameters.ContainsKey('Id')){
   if($Id -ne 4242){throw 'Unexpected process-ID read in offline fixture'}
   if(-not $script:fixtureAbsent){return $script:fixtureFakeGame};return
  }
  $allowed=@('BFBC2Game','BC2XrHost','BC2NativeTrace','BC2NativeIpcProbe','BC2NativeMenuProbe','SceneMenuSidecar421','SceneMenuSidecar431','SceneMenuSidecarR46','ExceptionWatch422','ProvisionRunner')
  foreach($value in $Name){if($value -cnotin $allowed){throw 'Unexpected process-name read in offline fixture'}}
  if('BFBC2Game' -cin $Name -and -not $script:fixtureAbsent){return $script:fixtureFakeGame}
 }
 function Start-Process {throw 'No real launches are allowed by this fixture'}
 function Stop-Process {throw 'No real process control is allowed by this fixture'}
 $value=$null;$errorText=$null
 try {
  . ([ScriptBlock]::Create($Fixture.adapted))
  $raw=Assert-Runtime02NativeCleanup -ExactSceneIdentity $Fixture.identity -NativeRunDirectory $Fixture.run -RuntimeBinding $Fixture.binding -StockModuleSnapshotPin $Fixture.stock
  $value=$raw|ConvertFrom-Json -AsHashtable -DateKind String -Depth 64
 } catch {$errorText=$_.Exception.Message}
 return @{value=$value;error=$errorText;mock_process_reads=$script:fixtureMockCalls;refresh_calls=$fake.RefreshCalls;dispose_calls=$fake.DisposeCalls}
}

$cases=@(
 @{name='matching-live';fault='none';accept=$true;absent=$false},
 @{name='missing-result';fault='missing_result';accept=$false;absent=$false;message='Cleanup path missing:';detail='trace-result.json'},
 @{name='hooks-still-enabled';fault='hooks_enabled';accept=$false;absent=$false;message='Cleanup boolean differs: hooks_disabled';detail=''},
 @{name='native-not-drained';fault='undrained';accept=$false;absent=$false;message='Cleanup boolean differs: drained';detail=''},
 @{name='wrong-original-lifetime';fault='wrong_lifetime';accept=$false;absent=$false;message='Cleanup belongs to another game lifetime';detail=''},
 @{name='changed-probe-pin';fault='changed_pin';accept=$false;absent=$false;message='Cleanup file pin changed:';detail='BC2NativeProbe.dll'},
 @{name='absent-original';fault='none';accept=$true;absent=$true}
)
$results=[Collections.Generic.List[object]]::new()
foreach($case in $cases){
 $observation=$null;$fixture=$null;$failure=$null
 try {
  $fixture=New-CleanupFixture $case.name $case.fault
  $observation=Invoke-SyntheticAssertion $fixture $case.absent
  if($case.accept){
   Need ($null -eq $observation.error) ('Expected admission: '+$observation.error)
   $r=$observation.value
   Need ($r.schema -ceq 'fvr.runtime02.native-cleanup-binding.v1' -and $r.native_cleanup_confirmed -eq $true) 'Missing cleanup binding'
   Need ($r.original_process_absent -eq $case.absent -and $r.live_modules_verified -eq (-not $case.absent)) 'Wrong live/absent original classification'
   Need ($r.normal_exit_observed -eq $false -and $r.game_close_performed -eq $false -and $r.profiles_restored -eq $false -and $r.forced_termination -eq $false) 'Read-only validation claimed a close, normal exit or restoration'
   Need ($r.native_reload_accepted -eq $false -and $r.headset_verified -eq $false -and $r.overall_test_pass_required_for_cleanup -eq $false) 'Synthetic cleanup implied native acceptance or required mechanical success'
   Need ($r.cli_exit -eq $fixture.cli_exit -and $observation.mock_process_reads -gt 0) 'Original CLI observation or process mock was skipped'
   if($case.absent){Need ($null -eq $r.module_paths -and $observation.refresh_calls -eq 0 -and $observation.dispose_calls -eq 0) 'Absent recovery performed a live-module join'}
   else {Need ($observation.refresh_calls -gt 0 -and $observation.dispose_calls -eq 1) 'Live validation did not retain/recheck/dispose its fake original'}
   Fixture-Json (Join-Path $fixture.case 'synthetic-binding.json') $r
  }else{
   Need ($null -eq $observation.value -and -not [string]::IsNullOrEmpty($observation.error)) 'Unsafe synthetic evidence was admitted'
   Need ($observation.error.Contains($case.message) -and $observation.error.Contains($case.detail)) ('Refused for the wrong reason: '+$observation.error)
  }
 }catch{$failure=$_.Exception.Message}
 $results.Add(@{case=$case.name;synthetic=$true;expected_admission=$case.accept;expected_original_absent=$case.absent;passed=($null -eq $failure);failure=$failure;observation=$observation})
}
$totalBytes=[long]((Get-ChildItem -LiteralPath $Output -File -Recurse|Measure-Object Length -Sum).Sum)
$passed=@($results|Where-Object {-not $_.passed}).Count -eq 0 -and $totalBytes -lt 4MB
$summary=@{schema='fvr.runtime02.synthetic-native-cleanup-tests.v1';synthetic=$true;helper=(Fixture-Pin $helper);cases=@($results.ToArray());passed=$passed;fixture_bytes_before_summary=$totalBytes;fixture_limit_bytes=4MB;real_process_enumerations=0;game_actions=0;native_acceptance=$false;headset_acceptance=$false;fixtures_retained=$true;output=$Output}
Fixture-Json (Join-Path $Output 'result.json') $summary
Write-Output (Join-Path $Output 'result.json')
if(-not $passed){throw 'Synthetic native-cleanup regression failed; retained result.json identifies the case and reason'}

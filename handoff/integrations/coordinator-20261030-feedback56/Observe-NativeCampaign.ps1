#requires -Version 7.5
<#
One root-owned, headset-free saved-campaign observation. Root reviews its menu image before request1. Never installs or selects an arbitrary
checkpoint, changes graphics, activates a window, or force-terminates. Reviewed Singleplayer/Resume clicks and an optional reviewed campaign confirmation are allowed.
Root must freeze the new normal runtime and separately attest its menu helper:
  schema: fvr.feedback56.campaign-helper-build.v1
  build_receipt, executable, menu_source, x64_build_result: {path,bytes,sha256}
  source_commit, source_sha256: the completed build's exact source identities.
The menu source and executable must be canonical; the x64 result must be the
successful matching feedback56 build. Pass both receipt hashes explicitly.
#>
[CmdletBinding()]
param(
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$BuildReceiptSha256,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedSourceCommit,
 [Parameter(Mandatory)][string]$MenuProvenancePath,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$MenuProvenanceSha256,
 [ValidatePattern('^native-campaign[0-9]{2}$')][string]$RunName='native-campaign01'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo='G:\Unity\VRMMO\FrostbiteVR'
$parent=Join-Path $repo 'reports/coordinator-20261030-feedback56'
$run=Join-Path $parent $RunName
$buildPath=Join-Path $parent 'normal-runtime/build-receipt.json'
$menuExe=Join-Path $parent 'campaign-helper06/NativeCampaignObserve.exe'
$gameDirectory='G:\New folder (2)\Battlefield Bad Company 2'
$gameExe=Join-Path $gameDirectory 'BFBC2Game.exe'
$gameSha='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
$profile='C:\Users\outca\Documents\BFBC2'
$launcher='G:\Unity\VRMMO\BC2VR-Launcher'
$leasePath='G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
$leaseToken='feedback56-b739ab9dc1fb4bf5bcdc385e091aadcd'
$rawLimit=1920L*1080L*4L
# Normal native output can retain three bounded world images plus five desktop
# images. Original graphics are checked below and never enlarged by this helper.
$nativeRawLimit=3L*4096L*4096L*4L+5L*$rawLimit
$allRawLimit=$nativeRawLimit+5L*$rawLimit
$pins=[Collections.Generic.List[object]]::new()
$locks=[Collections.Generic.List[IDisposable]]::new()
$owned=[Collections.Generic.List[object]]::new()
$profiles=@();$launcherFiles=@();$stock=@();$game=$null;$menu=$null;$trace=$null
$traceAttempted=$false;$retired=$false;$restored=$false;$launcherPreserved=$false
$runCreated=$false;$failure=$null;$cleanupFailures=[Collections.Generic.List[string]]::new()
$stage='storage';$clock=$null;$observationPassed=$false;$menuResult=$null
$close=[ordered]@{normal_close_observed=$false;original_already_absent=$false;close_request_accepted=$false;exit_observed=$false;exit_code=$null;forced_termination=$false}
$oldTemp=@{};foreach($key in 'TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE'){$oldTemp[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
function Full([string]$Path){if(-not [IO.Path]::IsPathFullyQualified($Path)){throw 'Absolute path required'};[IO.Path]::GetFullPath($Path).TrimEnd('\')}
function SamePath([string]$Actual,[string]$Expected){if((Full $Actual) -ine (Full $Expected)){throw "Path differs: $Actual"}}
function NoLinks([string]$Path){$p=Full $Path;while($p){if((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked path refused: $p"};$p=Split-Path -Parent $p}}
function Pin([string]$Path,[long]$Maximum=4MB){
 $Path=Full $Path;NoLinks $Path;$item=Get-Item -LiteralPath $Path -Force
 if($item.PSIsContainer -or $item.Length -gt $Maximum){throw "Oversized or non-file evidence: $Path"}
 $size=$item.Length;$modified=$item.LastWriteTimeUtc;$sha=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
 $after=Get-Item -LiteralPath $Path -Force
 if($after.Length -ne $size -or $after.LastWriteTimeUtc -ne $modified){throw "File changed during hashing: $Path"}
 @{path=$Path;bytes=$size;sha256=$sha}
}
function CheckPin($Expected,[string]$Path,[long]$Maximum=4MB){
 SamePath $Expected.path $Path
 if($Expected.sha256 -cnotmatch '^[a-f0-9]{64}$' -or ($Expected.bytes -isnot [int] -and $Expected.bytes -isnot [long])){throw 'Malformed file pin'}
 $actual=Pin $Path $Maximum
 if($actual.bytes -ne $Expected.bytes -or $actual.sha256 -cne $Expected.sha256){throw "Pinned file changed: $Path"}
 $pins.Add($actual);return $actual
}
function ReadJson([string]$Path,[long]$Maximum=4MB){$p=Pin $Path $Maximum;if($p.bytes -eq 0){throw "Empty JSON: $Path"};$value=[IO.File]::ReadAllText($Path)|ConvertFrom-Json -AsHashtable -Depth 64 -DateKind String;if($value -isnot [Collections.IDictionary]){throw "JSON object required: $Path"};$null=CheckPin $p $Path $Maximum;return $value}
function Save([string]$Name,$Value){
 $path=Join-Path $run $Name;NoLinks $path
 $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 40));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function RequireBool($Value,[string]$Name,[bool]$Expected){if(-not $Value.Contains($Name) -or $Value[$Name] -isnot [bool] -or $Value[$Name] -ne $Expected){throw "Required boolean differs: $Name"}}
function RequireZero($Value,[string]$Name){if(-not $Value.Contains($Name) -or ($Value[$Name] -isnot [int] -and $Value[$Name] -isnot [long]) -or $Value[$Name] -ne 0){throw "Required zero differs: $Name"}}
function RequireFlag($Value,[string]$Name,[bool]$Expected){
 if(-not $Value.Contains($Name) -or ($Value[$Name] -isnot [bool] -and $Value[$Name] -isnot [int] -and $Value[$Name] -isnot [long]) -or [int]$Value[$Name] -ne [int]$Expected){throw "Required binary flag differs: $Name"}
}
function Lease {
 $value=ReadJson $leasePath 64KB
 if($value.owner_token -cne $leaseToken -or $value.run -cne 'coordinator-20261030-feedback56'){throw 'Root does not own the feedback56 integration lease'}
}
function Idle([switch]$AllowOwned){
 $names=@('BFBC2Game','BC2XrHost','BC2NativeTrace','BC2NativeIpcProbe','BC2NativeMenuProbe','BC2NativeResolution','SceneMenuSidecar421','SceneMenuSidecar431','SceneMenuSidecarR46','ExceptionWatch422','ProvisionRunner','cl','link','cmake','ninja','ctest','MSBuild')
 foreach($p in @(Get-Process -Name $names -ErrorAction SilentlyContinue)){
  $match=@($owned|Where-Object {$_.identity.pid -eq $p.Id})
  if(-not $AllowOwned -or $match.Count -ne 1){throw "Shared game/helper/build active: $($p.ProcessName) PID $($p.Id)"}
  AssertProcess $match[0]
 }
}
function Desktop {
 $d=(& (Join-Path $repo 'handoff/integrations/coordinator-20261028-runtime02/DesktopContext.ps1')|ConvertFrom-Json -AsHashtable -DateKind String)
 if($d.same_input_desktop -ne $true -or $d.station -cne 'WinSta0' -or $d.process_session -ne $d.console_session){throw 'Caller is not on the active interactive input desktop'}
 return $d
}
function Deadline {if($clock -and $clock.ElapsedMilliseconds -ge 200000){throw 'The 200-second session deadline expired; no further native action'}}
function RecheckPins {foreach($p in @($pins.ToArray()|Sort-Object path -Unique)){$fresh=Pin $p.path ([Math]::Max(1L,[long]$p.bytes));if($fresh.sha256 -cne $p.sha256 -or $fresh.bytes -ne $p.bytes){throw "Evidence changed: $($p.path)"}}}
function AssertProcess($Entry){
 $p=$Entry.process;$p.Refresh();if($p.HasExited){throw 'Retained original process exited'}
 $identity=[Feedback56Process]::Identity($p.SafeHandle)
 if($identity.pid -ne $Entry.identity.pid -or $identity.creation_filetime -cne $Entry.identity.creation_filetime){throw 'Retained process lifetime differs'}
 SamePath $identity.path $Entry.identity.path
 if((Pin $identity.path 128MB).sha256 -cne $Entry.identity.sha256){throw 'Retained process executable bytes differ'}
}
function StartOwned([string]$Path,[string[]]$Arguments,[string]$Name,[switch]$Game){
 Deadline;Lease;Idle -AllowOwned
 $pinned=Pin $Path 128MB
 $launch=@{FilePath=$Path;WorkingDirectory=(Split-Path -Parent $Path);PassThru=$true;WindowStyle='Hidden'}
 if($Arguments.Count){$launch.ArgumentList=@($Arguments|ForEach-Object {if($_.Contains('"')){throw 'Unexpected quote in child argument'};'"'+$_+'"'})}
 if($Game){$launch.WindowStyle='Normal'}else{$launch.RedirectStandardOutput=Join-Path $run ($Name+'-stdout.json');$launch.RedirectStandardError=Join-Path $run ($Name+'-stderr.txt')}
 $p=Start-Process @launch
 # Keep the returned Process and its handle through every observation and close.
 $null=$p.Handle
 $entry=@{process=$p;identity=@{pid=$p.Id;path=$Path;sha256=$pinned.sha256;creation_filetime=$null;root_launched=$true;original_handle_retained=$true};name=$Name}
 $owned.Add($entry)
 $actual=[Feedback56Process]::Identity($p.SafeHandle);SamePath $actual.path $Path
 $entry.identity.creation_filetime=$actual.creation_filetime
 $entry.identity.start_utc=[DateTime]::FromFileTimeUtc([long]$actual.creation_filetime).ToString('o')
 Save ($Name+'-process.json') $entry.identity
 return $entry
}
function WaitOwned($Entry,[int]$Milliseconds){
 $until=[Environment]::TickCount64+$Milliseconds
 while(-not $Entry.process.HasExited -and [Environment]::TickCount64 -lt $until){Deadline;Start-Sleep -Milliseconds 100;$Entry.process.Refresh()}
 if(-not $Entry.process.HasExited){throw "Owned $($Entry.name) exceeded its finite wait; left untouched"}
 Save ($Entry.name+'-exit.json') @{identity=$Entry.identity;retained_handle_exit_observed=$true;exit_code=$Entry.process.ExitCode;forced_termination=$false;utc=[DateTime]::UtcNow.ToString('o')}
}
function Modules($Entry){
 AssertProcess $Entry
 $rows=@($Entry.process.Modules|ForEach-Object {@{name=$_.ModuleName;path=$_.FileName;base=[string]$_.BaseAddress.ToInt64();bytes=$_.ModuleMemorySize}})
 if($rows.Count -eq 0 -or $rows.Count -gt 1024){throw 'Invalid bounded module snapshot'}
 return ,$rows
}
function ProfileIdle {Lease;Idle;if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'A game owns profiles; no restoration'}}
function Backup([string]$Source,[string]$Relative){
 $before=Pin $Source 4MB;$destination=Join-Path $run $Relative
 $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination));[IO.File]::Copy($Source,$destination,$false)
 $copy=Pin $destination 4MB
 if($copy.sha256 -cne $before.sha256 -or (Pin $Source 4MB).sha256 -cne $before.sha256){throw 'Backup did not retain exact original bytes'}
 @{original=$before;backup=$copy}
}
function ProveRetirement {
 if(-not $traceAttempted){return}
 if(-not $trace -or -not $trace.process.HasExited){throw 'Original native trace has not exited; preserve game and profiles'}
 $bootstrap=ReadJson (Join-Path $run 'trace-stdout.json') 4MB
 if($bootstrap.schema -cne 'bc2vr.native-run-outcome.v1' -or $bootstrap.pid -ne $game.identity.pid -or $bootstrap.outcome -cne 'remote_thread_exited'){throw 'Remote native thread exit is not proven'}
 RequireBool $bootstrap 'remote_thread_exit_observed' $true;RequireBool $bootstrap 'report_nonempty' $true
 $report=ReadJson (Join-Path $run 'native-trace.json') 96MB
 # This exact failure precedes global hook activation in the pinned source.
 # It is safe to close our own game, but never a successful observation.
 if($bootstrap.probe_exit -eq 20 -and $report.state -ceq 'failed' -and $report.error -ceq 'Main-menu startup requires native menu capture and input'){
  RequireBool $report 'hooks_disabled' $true
  $script:retired=$true
  Save 'native-retirement.json' @{original_game=$game.identity;bootstrap=(Pin (Join-Path $run 'trace-stdout.json'));native_report=(Pin (Join-Path $run 'native-trace.json'));remote_thread_exited=$true;hooks_disabled=$true;pre_activation_refusal=$true;native_observation_passed=$false;forced_termination=$false}
  return
 }
 RequireZero $bootstrap 'probe_exit'
 if($report.pid -ne $game.identity.pid){throw 'Native report belongs to another game'}
 RequireBool $report 'hooks_disabled' $true;RequireBool $report.gameplay.reload_flow 'drained' $true;RequireZero $report.gameplay.reload_flow 'in_flight'
 $script:retired=$true
 Save 'native-retirement.json' @{original_game=$game.identity;bootstrap=(Pin (Join-Path $run 'trace-stdout.json'));native_report=(Pin (Join-Path $run 'native-trace.json') 96MB);remote_thread_exited=$true;probe_exit=0;remote_thread_exit_observed=$true;report_nonempty=$true;hooks_disabled=$true;reload_flow_drained=$true;in_flight=0;trace_exit=$trace.process.ExitCode;forced_termination=$false}
}
try {
 NoLinks $parent;NoLinks $run
 if(-not (Test-Path -LiteralPath $parent -PathType Container) -or (Test-Path -LiteralPath $run)){throw 'Existing feedback56 parent and a fresh run directory required'}
 . (Join-Path $repo 'tools/StorageBudget.ps1')
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes ($allRawLimit+128MB) -RawCaptureBytes $allRawLimit -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees',$launcher)
 $null=[IO.Directory]::CreateDirectory($run);$runCreated=$true;Save 'storage-session.json' $budget
 $stage='admission';Lease;Idle
 $ambient=@(Get-ChildItem Env:|Where-Object {$_.Name -match '^(FVR_|BC2_)' -and $_.Value})
 if($ambient.Count){throw ('Stock launch refuses inherited native overrides: '+($ambient.Name -join ', '))}
 $desktop=Desktop;Save 'desktop-before.json' $desktop
 $buildPin=Pin $buildPath
 if($buildPin.sha256 -cne $BuildReceiptSha256){throw 'Reviewed new runtime receipt hash differs'}
 $build=ReadJson $buildPath
 if($build.schema -cne 'fvr.checkpoint436.normal-build.v1' -or $build.built_source_commit -cne $ExpectedSourceCommit){throw 'New normal build source identity differs'}
 RequireBool $build 'normal' $true;RequireBool $build 'full_normal_builds' $true;RequireBool $build 'diagnostic_only' $false;RequireBool $build 'operation_records_changed' $false
 $null=CheckPin $build.source_freeze (Join-Path $parent 'normal-runtime/source-freeze.json')
 $freeze=ReadJson $build.source_freeze.path
 if($freeze.source_commit -cne $ExpectedSourceCommit -or $freeze.source_sha256 -cne $build.source_sha256){throw 'Source freeze differs'}
 $null=CheckPin $freeze.source_receipt $build.source_receipt
 $source=ReadJson $build.source_receipt
 if($source.source_sha256 -cne $build.source_sha256){throw 'Source closure differs'}
 foreach($row in @($source.files)+@($source.excluded_data_sha256)){
  $sourcePath=[IO.Path]::GetFullPath((Join-Path $repo $row.path))
  if(-not $sourcePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Source closure escaped canonical checkout'}
  $actual=Pin $sourcePath 16MB;if($actual.sha256 -cne $row.sha256){throw "Source changed: $($row.path)"};$pins.Add($actual)
 }
 $null=CheckPin $build.reviewed_header $freeze.reviewed_header.path
 foreach($arch in 'x86','x64'){
  $null=CheckPin $build.normal_cache[$arch].cache (Join-Path $repo "build/$arch/CMakeCache.txt")
  $null=CheckPin $build.normal_cache[$arch].operation_config (Join-Path $repo "build/$arch/generated/bc2-operation/Bc2NativeOperationBuild.generated.h")
  if([IO.File]::ReadAllText($build.normal_cache[$arch].cache.path) -match '(?m)^BC2_[^:\r\n]*_PROBE:[^=\r\n]+=ON\r?$'){throw 'Diagnostic build flag enabled'}
 }
 foreach($key in $build.binaries.Keys){$null=CheckPin $build.binaries[$key] (Join-Path $parent ('normal-runtime/'+[IO.Path]::GetFileName($build.binaries[$key].path))) 128MB}
 $provenancePin=Pin $MenuProvenancePath
 if($provenancePin.sha256 -cne $MenuProvenanceSha256){throw 'Menu provenance hash differs'}
 $provenance=ReadJson $MenuProvenancePath
 if($provenance.schema -cne 'fvr.feedback56.campaign-helper-build.v1' -or $provenance.source_commit -cne $ExpectedSourceCommit -or $provenance.source_sha256 -cne $build.source_sha256){throw 'Menu helper provenance does not match this normal build'}
 $null=CheckPin $provenance.build_receipt $buildPath
 $null=CheckPin $provenance.executable $menuExe 128MB
 $null=CheckPin $provenance.menu_source 'G:/Unity/VRMMO/bc2vr-task-worktrees/campaign-coordinator-20261028-r1/handoff/integrations/coordinator-20261030-feedback56/NativeCampaignObserve.cpp'
 $resultPath=Full $provenance.x64_build_result.path
 if((Split-Path -Parent $resultPath) -ine $parent -or (Split-Path -Leaf $resultPath) -cnotmatch '^x64-[A-Za-z0-9-]+-result\.json$'){throw 'New x64 build result required'}
 $null=CheckPin $provenance.x64_build_result $resultPath
 $buildResult=ReadJson $resultPath;RequireBool $buildResult 'passed' $true
 if($buildResult.source_commit -cne $ExpectedSourceCommit -or $buildResult.source_sha256 -cne $build.source_sha256){throw 'Menu helper build source differs'}
 $gamePin=Pin $gameExe 128MB;if($gamePin.sha256 -cne $gameSha){throw 'Reviewed game image differs'};$pins.Add($gamePin)
 foreach($path in @($buildPath,$build.binaries.probe.path,$build.binaries.trace.path,$menuExe,$provenance.menu_source.path,$gameExe)){$locks.Add([IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))}
 Save 'artifact-binding.json' @{build=$buildPin;menu_provenance=$provenancePin;menu=$provenance.executable;source_commit=$ExpectedSourceCommit;source_sha256=$build.source_sha256;game=$gamePin;input_protocol=$build.input_protocol}
 $cacheSource='G:\Unity\VRMMO\bc2vr-task-worktrees\campaign-coordinator-20261028-r1\reports\coordinator-20261030-wiring54\feed-cache01\body-ammo.fvrprop'
 $cachePin=Pin $cacheSource 64MB
 if($cachePin.sha256 -cne '7e3d16f5bcc0af74ff7ab3afe2740197b1b4b9ac18dc9f61416325861af8f49e'){throw 'Verified 264-part body cache differs'}
 $cacheTarget=Join-Path $run 'body-ammo-assets.fvrprop'
 [IO.File]::Copy($cacheSource,$cacheTarget,$false)
 $stagedCache=Pin $cacheTarget 64MB
 if($stagedCache.sha256 -cne $cachePin.sha256){throw 'Body cache staging differs'}
 Save 'body-cache.json' @{source=$cachePin;staged=$stagedCache;parts=264;generated_again=$false}
 $stage='preserve-originals';ProfileIdle
 foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){$profiles+=Backup (Join-Path $profile $name) ('profile-before/'+$name)}
 foreach($relative in 'active.json','data/settings.json'){$launcherFiles+=Backup (Join-Path $launcher $relative) ('launcher-before/'+$relative)}
 Save 'originals.json' @{profiles=$profiles;launcher=$launcherFiles;checkpoint_changed=$false;graphics_changed=$false;native_environment_changed=$false}
 $settings=[IO.File]::ReadAllText((Join-Path $profile 'settings.ini'));$dimensions=@{}
 foreach($name in 'Width','Height'){$matches=[regex]::Matches($settings,('(?m)^'+$name+'=([0-9]+)\r?$'));if($matches.Count -ne 1){throw 'One existing original graphics dimension required'};$dimensions[$name]=[int]$matches[0].Groups[1].Value}
 if($dimensions.Width -lt 1 -or $dimensions.Width -gt 1920 -or $dimensions.Height -lt 1 -or $dimensions.Height -gt 1080 -or [long]$dimensions.Width*$dimensions.Height*4 -gt $rawLimit){throw 'Original graphics exceed the single 1920x1080x4 capture budget; no graphics changes permitted'}
 Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public static class Feedback56Process {
 [StructLayout(LayoutKind.Sequential)] struct Time {public uint low,high;}
 public sealed class Record {public uint pid;public string path;public string creation_filetime;}
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)] struct Last {public uint size,tick;}
 delegate bool MonitorCallback(IntPtr monitor,IntPtr dc,ref Rect rect,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorCallback callback,IntPtr data);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out Rect rect);
 [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref Last value);
 public static void RequireIdleInput(){var last=new Last{size=8};if(!GetLastInputInfo(ref last)||unchecked((uint)Environment.TickCount-last.tick)<3000)throw new InvalidOperationException("Recent physical input; no game launch or native attachment");}
 public static Rect ObserveWindow(IntPtr window){
  Rect outer;
  if(!GetWindowRect(window,out outer)||outer.right<=outer.left||outer.bottom<=outer.top)throw new InvalidOperationException("Game window geometry unavailable");
  return outer;
 }
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint GetProcessId(SafeProcessHandle h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageNameW(SafeProcessHandle h,uint flags,StringBuilder path,ref uint count);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(SafeProcessHandle h,out Time created,out Time exited,out Time kernel,out Time user);
 public static Record Identity(SafeProcessHandle h){
  uint pid=GetProcessId(h);var path=new StringBuilder(32768);uint count=(uint)path.Capacity;Time birth,exit,kernel,user;
  if(pid==0||!QueryFullProcessImageNameW(h,0,path,ref count)||!GetProcessTimes(h,out birth,out exit,out kernel,out user))throw new Win32Exception(Marshal.GetLastWin32Error());
  return new Record{pid=pid,path=path.ToString(),creation_filetime=(((ulong)birth.high<<32)|birth.low).ToString(System.Globalization.CultureInfo.InvariantCulture)};
 }
}
'@
 RecheckPins;Lease;Idle;$null=Desktop;[Feedback56Process]::RequireIdleInput()
 foreach($entry in $profiles+$launcherFiles){if((Pin $entry.original.path).sha256 -cne $entry.original.sha256){throw 'Original profile/launcher changed before stock launch'}}
 $clock=[Diagnostics.Stopwatch]::StartNew();$stage='stock-launch'
 $game=StartOwned $gameExe @() 'game' -Game
 $until=[Environment]::TickCount64+15000
 do{AssertProcess $game;if($game.process.MainWindowHandle -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}while([Environment]::TickCount64 -lt $until)
 if($game.process.MainWindowHandle -eq [IntPtr]::Zero){throw 'Original game has no window within 15 seconds'}
 $stock=Modules $game
 if(@($stock|Where-Object {$_.name -like 'BC2*.dll' -or $_.name -like '*NativeProbe*' -or $_.name -like '*TestRange*' -or $_.name -like '*PreWorldMsaa*'}).Count){throw 'Stock game already contains native diagnostic modules'}
 Save 'stock-modules.json' @{original_game=$game.identity;modules=$stock;observed_utc=[DateTime]::UtcNow.ToString('o')}
 Save 'original-window.json' @{original_game=$game.identity;rect=[Feedback56Process]::ObserveWindow($game.process.MainWindowHandle);placement_changed=$false;graphics_changed=$false;activation_requested=$false}
 $stage='capture-admission';$captureBudget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Capture -PlannedBytes ($allRawLimit+128MB) -RawCaptureBytes $allRawLimit -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees',$launcher)
 Save 'storage-capture.json' $captureBudget;Deadline;$null=Desktop;RecheckPins
 $stage='menu-start';$menuFolder=Join-Path $run 'menu'
 $menu=StartOwned $menuExe @('--output',$menuFolder) 'menu'
 $channelPath=Join-Path $menuFolder 'channel.txt';$until=[Environment]::TickCount64+5000
 while(-not(Test-Path -LiteralPath $channelPath) -and [Environment]::TickCount64 -lt $until){AssertProcess $menu;Start-Sleep -Milliseconds 50}
 if(-not(Test-Path -LiteralPath $channelPath)){throw 'Menu channel was not published within 5 seconds'}
 $channelPin=Pin $channelPath 4KB;$channel=[IO.File]::ReadAllText($channelPath).Trim()
 if($channel -cnotmatch '^[A-Za-z0-9_-]+$'){throw 'Menu channel contains invalid characters'}
 $stage='native-trace';AssertProcess $game;AssertProcess $menu;Lease;Idle -AllowOwned;$null=Desktop;RecheckPins
 [Feedback56Process]::RequireIdleInput();$null=[Feedback56Process]::ObserveWindow($game.process.MainWindowHandle)
 # NativeTrace validates a maximum of60 seconds, independently of the helper's120-second lifetime.
 $arguments=@('--game',$gameDirectory,'--dll',$build.binaries.probe.path,'--report',(Join-Path $run 'native-trace.json'),'--stream',$channel,'--seconds','60','--body-inventory','--magazine-reload')
 Save 'trace-invocation.json' @{original_game=$game.identity;menu=$menu.identity;channel=$channelPin;build_receipt=$buildPin;probe=$build.binaries.probe;trace=$build.binaries.trace;arguments=$arguments;native_seconds=60;root_owned=$true;forced_termination=$false;utc=[DateTime]::UtcNow.ToString('o')}
 # Once attachment is possible, every close requires the independent native proof.
 $traceAttempted=$true;$trace=StartOwned $build.binaries.trace.path $arguments 'trace'
 WaitOwned $trace 150000;WaitOwned $menu 125000
 $stage='native-retirement';ProveRetirement
 $stage='menu-result';$menuResult=ReadJson (Join-Path $menuFolder 'result.json') 64KB
 RequireBool $menuResult 'root_request1_accepted' $true;RequireBool $menuResult 'root_request2_accepted' $true;RequireBool $menuResult 'headset_tested' $false
 if(Test-Path -LiteralPath (Join-Path $menuFolder 'menu.rgba')){
  $image=ReadJson (Join-Path $menuFolder 'image.json') 64KB
  if($image.width -lt 1 -or $image.width -gt 1920 -or $image.height -lt 1 -or $image.height -gt 1080){throw 'Menu image exceeded admitted dimensions'}
  $imagePin=Pin (Join-Path $menuFolder 'menu.rgba') $rawLimit
  if($imagePin.bytes -ne [long]$image.width*$image.height*4){throw 'Menu image byte count differs'}
  Save 'image-pin.json' @{image=$imagePin;metadata=$image;maximum_images=1;visual_acceptance=$false}
 }
 $observationPassed=($trace.process.ExitCode -eq 0 -and $menu.process.ExitCode -eq 0 -and $menuResult.passed_observation -eq $true)
 if(-not $observationPassed){throw 'Finite menu observation did not pass; retained evidence is not acceptance'}
} catch {
 $failure=@{stage=$stage;message=$_.Exception.Message;utc=[DateTime]::UtcNow.ToString('o')}
} finally {
 if($runCreated){
  if($failure){try{Save 'failure.json' $failure}catch{$cleanupFailures.Add('Failure receipt: '+$_.Exception.Message)}}
  try {
   # Never infer remote retirement from CLI exit or a missing process alone.
   if($traceAttempted -and -not $retired){ProveRetirement}
   foreach($entry in @($trace,$menu)){if($entry -and -not $entry.process.HasExited){WaitOwned $entry 5000}}
   if($game){
    $game.process.Refresh()
    if($game.process.HasExited){$close.original_already_absent=$true;Save 'game-exited-before-close.json' @{original_game=$game.identity;retained_handle_exit_observed=$true;exit_code=$game.process.ExitCode;normal_close_observed=$false;close_requested=$false}}
    else {
     if($traceAttempted -and -not $retired){throw 'Native hooks may still be active; no close'}
     Lease;Idle -AllowOwned;$null=Desktop;AssertProcess $game;RecheckPins
     $live=Modules $game;$probeCount=0
     # A pre-attachment launch/window failure still allows a stock normal close.
     # Never manufacture a stock baseline after an attempted native attachment.
     if(-not $traceAttempted -and $stock.Count -eq 0){
      if(@($live|Where-Object {$_.name -like 'BC2*.dll' -or $_.name -like '*NativeProbe*' -or $_.name -like '*TestRange*' -or $_.name -like '*PreWorldMsaa*'}).Count){throw 'Unknown native module after stock launch failure; no close'}
      $stock=$live
     }
     foreach($module in $live){
      if($traceAttempted -and $module.path -ieq $build.binaries.probe.path){$probeCount++;continue}
      if(@($stock|Where-Object {$_.path -ieq $module.path -and $_.name -ieq $module.name}).Count -ne 1 -and
         ($module.name -like 'BC2*.dll' -or $module.name -like '*NativeProbe*' -or $module.name -like '*TestRange*' -or $module.name -like '*PreWorldMsaa*')){throw ('Another native module before close: '+$module.name)}
     }
     if($traceAttempted -and $probeCount -ne 1){throw 'Exactly one bound probe module required before close'}
     Save 'modules-before-close.json' @{original_game=$game.identity;modules=$live;native_retirement_proven=$retired}
     # Observation deadline stops new work, not verified cleanup of our own game.
     Lease;AssertProcess $game
     $close.close_request_accepted=$game.process.CloseMainWindow()
     Save 'close-request.json' @{original_game=$game.identity;accepted=$close.close_request_accepted;forced_termination=$false;utc=[DateTime]::UtcNow.ToString('o')}
     if(-not $close.close_request_accepted){throw 'Original game refused CloseMainWindow; profiles retained'}
     $close.exit_observed=$game.process.WaitForExit(10000)
     if(-not $close.exit_observed){throw 'Original game did not exit within 10 seconds; no force termination or restoration'}
     $close.exit_code=$game.process.ExitCode;$close.normal_close_observed=($close.exit_code -eq 0)
    }
   }
  }catch{$cleanupFailures.Add($_.Exception.Message)}
  try{Save 'close-outcome.json' $close}catch{$cleanupFailures.Add('Close receipt: '+$_.Exception.Message)}
  try {
   if($profiles.Count -gt 0){
    ProfileIdle
    foreach($entry in $profiles){$null=CheckPin $entry.backup $entry.backup.path}
    $after=@();foreach($entry in $profiles){$after+=Backup $entry.original.path ('profile-after/'+[IO.Path]::GetFileName($entry.original.path))}
    Save 'profile-after.json' $after
    foreach($entry in $profiles){
     ProfileIdle;NoLinks $entry.original.path
     if((Pin $entry.original.path).sha256 -cne $entry.original.sha256){[IO.File]::WriteAllBytes($entry.original.path,[IO.File]::ReadAllBytes($entry.backup.path))}
     if((Pin $entry.original.path).sha256 -cne $entry.original.sha256){throw 'Exact original profile restoration failed'}
    }
    $restored=($profiles.Count -eq 3)
   }
  }catch{$cleanupFailures.Add($_.Exception.Message)}
  try {
   $launcherAfter=@();foreach($entry in $launcherFiles){$current=Pin $entry.original.path;$launcherAfter+=$current;if($current.sha256 -cne $entry.original.sha256){throw 'Launcher active/settings changed; preserve backups without overwriting another owner'}}
   $launcherPreserved=($launcherFiles.Count -eq 2);Save 'launcher-after.json' @{unchanged=$launcherPreserved;files=$launcherAfter}
  }catch{$cleanupFailures.Add($_.Exception.Message)}
  $processOutcomes=@(foreach($entry in $owned){try{$entry.process.Refresh();@{identity=$entry.identity;name=$entry.name;has_exited=$entry.process.HasExited;exit_code=$(if($entry.process.HasExited){$entry.process.ExitCode}else{$null});forced_termination=$false}}catch{@{identity=$entry.identity;name=$entry.name;observation_error=$_.Exception.Message}}})
  try{Save 'outcome.json' @{schema='fvr.feedback56.native-campaign-observation.v1';run=$run;passed=($observationPassed -and -not $failure -and $cleanupFailures.Count -eq 0 -and $close.normal_close_observed -and $restored -and $launcherPreserved);failure=$failure;cleanup_failures=$cleanupFailures.ToArray();native_retirement_proven=$retired;close=$close;processes=$processOutcomes;exact_three_profiles_restored=$restored;launcher_active_settings_preserved=$launcherPreserved;profile_backups=$profiles;launcher_backups=$launcherFiles;native_trace_attempted=$traceAttempted;menu_result=$menuResult;maximum_native_seconds=60;maximum_observation_ms=200000;normal_close_wait_ms=10000;maximum_menu_images=3;maximum_world_images=2;maximum_native_images=8;maximum_images=13;elapsed_ms=$(if($clock){$clock.ElapsedMilliseconds}else{0});maximum_raw_image_bytes=$allRawLimit;stock_launch=$true;original_graphics=$true;checkpoint_changed=$false;gameplay_or_desktop_input_sent=$false;singleplayer_resume_optional_confirmation_authorized=$true;neutral_hover_published=[bool]$menu;native_environment_overrides_applied=$false;forced_termination=$false;headset_accepted=$false;visual_acceptance=$false;utc=[DateTime]::UtcNow.ToString('o')}}catch{$cleanupFailures.Add('Outcome receipt: '+$_.Exception.Message)}
 }elseif($failure){
  # Tiny refusal evidence survives even a storage refusal; no session is started.
  try{NoLinks $parent;if(Test-Path -LiteralPath $parent -PathType Container){$path=Join-Path $parent ($RunName+'-refusal-'+[Guid]::NewGuid().ToString('N')+'.json');[IO.File]::WriteAllText($path,($failure|ConvertTo-Json),[Text.UTF8Encoding]::new($false))}}catch{Write-Warning ('Refusal receipt could not be preserved: '+$_.Exception.Message)}
 }
 foreach($entry in $owned){$entry.process.Dispose()}
 foreach($stream in $locks){$stream.Dispose()}
 foreach($key in $oldTemp.Keys){[Environment]::SetEnvironmentVariable($key,$oldTemp[$key],'Process')}
}
if($failure -or $cleanupFailures.Count -or -not $close.normal_close_observed -or -not $restored -or -not $launcherPreserved){throw ('Menu observation incomplete; retain original receipts/backups at '+$run+'. '+$(if($failure){$failure.message})+' '+($cleanupFailures -join '; '))}
Write-Output (Join-Path $run 'outcome.json')

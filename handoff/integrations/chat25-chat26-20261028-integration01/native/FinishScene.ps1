#requires -Version 7.5
[CmdletBinding()]
param([string]$NativeCleanupReceipt,[ValidatePattern('^[a-f0-9]{64}$')][string]$NativeCleanupReceiptSha256)
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$nativeRoot=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01/native'
$run=Join-Path $nativeRoot 'scene01'
$profile='C:\Users\outca\Documents\BFBC2'
$exe='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
function Sha($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load($p,[long]$maximum=1MB){$file=Get-Item -LiteralPath $p;if($file.Length -gt $maximum -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected receipt'};Get-Content -LiteralPath $p -Raw|ConvertFrom-Json -DateKind String}
function Save($name,$value){$path=Join-Path $run $name;if(Test-Path -LiteralPath $path){throw 'Preserve prior finish evidence'};[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))}
function Below($path,$root){[IO.Path]::GetFullPath($path).StartsWith([IO.Path]::GetFullPath($root).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)}
function AssertPin($path,$hash){if($hash -notmatch '^[a-f0-9]{64}$' -or (Sha $path) -cne $hash){throw "Retained evidence changed: $path"}}
function AssertIdle {
 if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,SceneMenuSidecar421,SceneMenuSidecar431,SceneMenuSidecarR46,ExceptionWatch422,ProvisionRunner -ErrorAction SilentlyContinue){throw 'Native/session helper remains active; no close or restoration'}
}
$lease=Load 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
if($lease.owner_token -cne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'Another integration owns the lease'}
. (Join-Path $PSScriptRoot 'AssertKnifeCleanup427.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1');$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
AssertIdle
if(Test-Path -LiteralPath (Join-Path $run 'finish.json')){throw 'Scene already finished'}
$before=Load (Join-Path $run 'profile-before.json')
$expected=@('GameSettings.bin','GameSettings.ini','settings.ini')
if($before.profile_path -cne $profile -or @($before.files).Count -ne 3 -or
   ((@($before.files.name|Sort-Object)-join '|') -cne (($expected|Sort-Object)-join '|'))){throw 'Exact original three-profile set required'}
foreach($file in $before.files){AssertPin (Join-Path $run ('profile-before/'+$file.name)) $file.sha256}
$identityPath=Join-Path $run 'process.json';$id=$null;$game=$null
if(Test-Path -LiteralPath $identityPath){
 $id=Load $identityPath;Assert-KnifeProcessIdentity427 $id $exe
 $game=Get-Process -Id $id.pid -ErrorAction SilentlyContinue
 if($game -and ($game.Path -cne $exe -or [string]$game.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$id.creation_filetime)){throw 'Original game lifetime changed; preserve profile backups'}
}elseif(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'Game exists without retained original identity'}
$ownedModules=@();$bindings=@()
if($game){
 $modules=@($game.Modules)
 if(@($modules|Where-Object {$_.ModuleName -like '*PreWorldMsaa*' -or $_.ModuleName -like '*TestRange*' -or $_.ModuleName -like 'BC2ProvisionHarness*'}).Count){throw 'Unexpected retained helper'}
 $ownedModules=@($modules|Where-Object {$_.ModuleName -like 'BC2*.dll' -or $_.ModuleName -like '*NativeProbe*'})
 if(@($ownedModules|Where-Object {$_.ModuleName -notlike 'BC2NativeProbe*'}).Count){throw 'Unknown BC2 helper; do not close without its retirement proof'}
}
$admissions=@(Get-ChildItem -LiteralPath $nativeRoot -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'admission.json')})
$attempted=@($admissions|Where-Object {
 $admission=Load (Join-Path $_.FullName 'admission.json')
 $id -and $admission.pid -eq $id.pid -and [string]$admission.creation_filetime -ceq [string]$id.creation_filetime -and
 (Test-Path -LiteralPath (Join-Path $_.FullName 'trace-arguments.json'))
})
if($ownedModules.Count -or $attempted.Count -or $NativeCleanupReceipt){
 if(-not $id -or -not $NativeCleanupReceipt -or -not $NativeCleanupReceiptSha256 -or -not (Below $NativeCleanupReceipt $nativeRoot)){throw 'Last exact retained-probe cleanup receipt and hash required'}
 AssertPin $NativeCleanupReceipt $NativeCleanupReceiptSha256
 $cleanup=Load $NativeCleanupReceipt
 if($cleanup.schema -cne 'fvr.retained-probe-cleanup426.v1' -or $cleanup.pid -ne $id.pid -or
    [string]$cleanup.creation_filetime -cne [string]$id.creation_filetime -or $cleanup.modules -isnot [array]){throw 'Cleanup receipt belongs to another lifetime'}
 if($game -and $cleanup.modules.Count -ne $ownedModules.Count){throw 'Cleanup chain must cover every retained probe exactly'}
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $cleanup.modules){
  if(-not (Below $entry.path $nativeRoot) -or -not (Below $entry.native_report $nativeRoot) -or -not $seen.Add([IO.Path]::GetFullPath($entry.path))){throw 'Duplicate or foreign retained probe'}
  AssertPin $entry.path $entry.sha256;AssertPin $entry.native_report $entry.native_report_sha256;AssertPin $entry.trace_stdout $entry.trace_stdout_sha256
  if($game -and @($ownedModules|Where-Object {$_.FileName -ieq $entry.path}).Count -ne 1){throw 'Retained module does not match exact cleanup chain'}
  $native=Load $entry.native_report 96MB;$boot=Load $entry.trace_stdout
  Assert-KnifeNativeCleanup427 $native $boot $id.pid $entry.trace_exit (Read-KnifeCleanupProgress427 $entry.native_report)
  $episode=Split-Path $entry.native_report;$completionPath=Join-Path $episode 'completion.json';$completion=Load $completionPath
  if($completion.pid -ne $id.pid -or [string]$completion.creation_filetime -cne [string]$id.creation_filetime -or
     $completion.trace_exited -isnot [bool] -or -not $completion.trace_exited -or
     $completion.receiver_exited -isnot [bool] -or -not $completion.receiver_exited -or $completion.trace_exit -ne 0){throw 'Original native/receiver helper exits are not proven'}
  if($native.gameplay.reload_flow.drained -isnot [bool] -or -not $native.gameplay.reload_flow.drained -or $native.gameplay.reload_flow.in_flight -ne 0){throw 'Native gameplay dispatch not drained'}
  $bindings+=@{module=$entry.path;native_report_sha256=$entry.native_report_sha256;completion=$completionPath;completion_sha256=(Sha $completionPath);receiver_test_exit=$completion.receiver_exit}
 }
 foreach($attempt in $attempted){if(@($cleanup.modules|Where-Object {(Split-Path $_.native_report) -ieq $attempt.FullName}).Count -ne 1){throw 'A native attempt is missing from the final cleanup chain'}}
 Save 'native-cleanup-binding.json' @{receipt=$NativeCleanupReceipt;sha256=$NativeCleanupReceiptSha256;modules=$bindings;overall_test_pass_required_for_cleanup=$false}
}
if($game){
 AssertIdle
 $null=$game.CloseMainWindow()
 if(-not $game.WaitForExit(10000)){throw 'Owned game did not close normally; no kill or profile restoration'}
}
AssertIdle
if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'A game still owns the profile; no restoration'}
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
Save 'storage-finish.json' $budget
$after=Join-Path $run 'profile-after';if(Test-Path -LiteralPath $after){throw 'Preserve prior after-profile evidence'}
[IO.Directory]::CreateDirectory($after)|Out-Null
foreach($file in $before.files){
 $target=Join-Path $profile $file.name;$info=Get-Item -LiteralPath $target
 if($info.Length -gt 4MB -or ($info.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected profile before restoration'}
 Copy-Item -LiteralPath $target -Destination (Join-Path $after $file.name)
}
foreach($file in $before.files){
 $target=Join-Path $profile $file.name;$source=Join-Path $run ('profile-before/'+$file.name)
 [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($source));AssertPin $target $file.sha256
}
Save 'finish.json' @{schema='fvr.chat25-chat26.scene-cleanup.v1';closed=$true;forced=$false;original_game_identity_recorded=[bool]$id;exact_three_profiles_restored=$true;native_modules=$ownedModules.Count;native_retirement_bound=($bindings.Count -gt 0);checkpoint_seed_written=$false;handling_accepted=$false;headset_tested=$false}

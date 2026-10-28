#requires -Version 7.5
param(
 [Parameter(Mandatory)][ValidatePattern('^game[0-9]{2}$')][string]$OutputName,
 [Parameter(Mandatory)][string]$ProfileBackupReceipt,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ProfileBackupReceiptSha256
)
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$performance='G:\Unity\VRMMO\bc2vr-recovery\performance436'
function Sha([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function LoadJson([string]$path){Get-Content -LiteralPath $path -Raw|ConvertFrom-Json}
function AssertLease {
 $lease=LoadJson 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
 if($lease.owner_token -cne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'This integration does not own the native session lease'}
}
AssertLease
. (Join-Path $PSScriptRoot 'Dependencies.ps1')
Assert-KnifeIntegrationDependencies
if((Sha $ProfileBackupReceipt) -cne $ProfileBackupReceiptSha256){throw 'Profile backup receipt changed'}
$backup=LoadJson $ProfileBackupReceipt
foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){
 $entries=@($backup.files|Where-Object {$_.name -ceq $name})
 if($entries.Count -ne 1){throw 'Root must preserve all three profiles before this diagnostic'}
 $saved=Join-Path (Split-Path $ProfileBackupReceipt) ('profile-before/'+$name)
 if((Sha $saved) -cne $entries[0].sha256){throw "Preserved profile differs: $name"}
}
$output=Join-Path $performance $OutputName
if(Test-Path -LiteralPath $output){throw 'Preserve prior process evidence'}
. (Join-Path $repo 'tools/StorageBudget.ps1');$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
# The pinned legacy starter also guards its launch. This enclosing admission
# includes all current worker storage, which its historical wrapper predates.
$null=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $output -Operation Session -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
# Same normal diagnostic transport as the retained R56 setup, with only the two
# knife observers enabled. Stock pump/shell/cloud settings come from the existing launcher.
$flags=@{
 FVR_PRIVATE_TWO_SOURCE_SLOTS=$null;FVR_PRIVATE_THREE_SOURCE_SLOTS=$null;FVR_PRIVATE_EARLY_FRAME_ADMISSION=$null
 FVR_PRIVATE_PRESENT_EXPERIMENT436=$null;FVR_PRIVATE_PRESENT_PROGRESS_MS='0';FVR_PRIVATE_RENDER_QUEUE436='1'
 FVR_PRIVATE_GPU_TIMING436=$null;FVR_PRIVATE_GPU_PHASE_C_DELAY_MS=$null;FVR_PRIVATE_SOURCE_LOOKAHEAD=$null
 FVR_PRIVATE_EXPORT_OVERLAP='0';FVR_PRIVATE_GPU_ORDERED_PRESENT=$null;FVR_PRIVATE_HOST_COPY_SERVICE_MS=$null
 FVR_BC2_PRIVATE_CPU_ATTRIBUTION=$null;FVR_BC2_DETAILED_RIG_CAPTURE=$null;FVR_BC2_GRAPHICS_PROVENANCE=$null;FVR_BC2_DESTRUCTION_WITNESS=$null
 FVR_BC2_PRIVATE_KNIFE_SOUND_OBSERVE='1';FVR_BC2_PRIVATE_KNIFE_TIMING_OBSERVE='1';FVR_BC2_ATTACHED_CAMERA_OBSERVE=$null
 BC2_WEATHER_OBSERVER=$null;BC2_WEATHER_TRANSLATION_EXPERIMENT=$null;BC2_WEATHER_OBSERVER_DELAY_MS=$null
}
foreach($entry in Get-ChildItem Env:){
 if($entry.Name -match '^(FVR_PRIVATE_|FVR_BC2_)' -and -not $flags.ContainsKey($entry.Name) -and $entry.Value){throw "Unreviewed native diagnostic environment: $($entry.Name)"}
}
$before=@{}
try {
 foreach($key in $flags.Keys){$before[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$flags[$key],'Process')}
 AssertLease
 # Existing helper owns StorageBudget, asset cache, exact executable and left-monitor checks.
 & (Join-Path $performance 'StartMenuGameR4.ps1') -OutputName $OutputName -ReserveClientWidth 1920
 $original=Join-Path $output 'process.json'
 $identity=LoadJson $original
 $game=Get-Process -Id $identity.pid -ErrorAction Stop
 if($game.Path -cne $identity.path -or [string]$game.StartTime.ToUniversalTime().ToFileTimeUtc() -cne $identity.creation_filetime){throw 'Game lifetime changed during observed launch'}
 $identity|Add-Member -NotePropertyName observation_schema -NotePropertyValue 'fvr.bc2.knife-observation-process.v1'
 $identity|Add-Member -NotePropertyName observer_environment -NotePropertyValue $flags
 $identity|Add-Member -NotePropertyName original_process -NotePropertyValue @{path=$original;sha256=(Sha $original)}
 $identity|Add-Member -NotePropertyName observer_launcher -NotePropertyValue @{path=$PSCommandPath;sha256=(Sha $PSCommandPath)}
 $identity|Add-Member -NotePropertyName profile_backup -NotePropertyValue @{path=$ProfileBackupReceipt;sha256=$ProfileBackupReceiptSha256}
 $identity|Add-Member -NotePropertyName scope -NotePropertyValue 'Emulated D3D11 receiver diagnostic; no OpenXR or headset acceptance'
 [IO.File]::WriteAllText((Join-Path $output 'process-knife-observation.json'),($identity|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
} finally {
 foreach($key in $before.Keys){[Environment]::SetEnvironmentVariable($key,$before[$key],'Process')}
}

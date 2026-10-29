#requires -Version 7.5
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$run=Join-Path $repo 'reports/coordinator-20261029-prep07/run01'
$privateRoot=Join-Path $run 'preparation-launcher'
$primary='G:\Unity\VRMMO\BC2VR-Launcher'
$packaging=Join-Path $repo 'reports/coordinator-20261029-runtime03/package'
$zip=Join-Path $packaging 'BC2VR-0.1.16-win64.zip'
$zipHash='043f27944f151ad9c226dcd384080a424df1906ff0b7226f576c19111e939076'
$recipeHash='f31c3f3c1270f807f41b6947c2ffdb378528d0199c94e74c81212d39cbd09983'
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save($name,$value){
 $path=Join-Path $run $name
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value|ConvertTo-Json -Depth 12))
 $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Lease {
 $lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json
 if($lease.owner_token -cne 'campaign-coordinator-prep07-b68ed7bd465344e48ba49a89a4dc4ce0'){throw 'Integration lease differs'}
}
Lease
if(Test-Path -LiteralPath $run){throw 'Preserve prior candidate preparation'}
if((Sha $zip) -cne $zipHash){throw 'Frozen package differs'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 128MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
[IO.Directory]::CreateDirectory($run)|Out-Null
Save 'storage.json' $budget
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$primaryLock=Enter-FvrLock $primary
$privateLock=$null;$child=$null;$versionChild=$null;$stage='primary baseline'
$before=@{}
try {
 Assert-FvrIdle
 foreach($name in 'active.json','data/settings.json'){$before[$name]=Sha (Join-Path $primary $name)}
 if($before['active.json'] -cne '9b60ff5ce16e3c146a56d5410edbd43b9f91e393ab7851af41d9c6f3b800a8b2' -or $before['data/settings.json'] -cne 'be6f984f6bd44dcc62aab0a2ac1a63acaa2d3221d2919f2a44a4aeeef2b1f7cf'){throw 'Primary launcher baseline changed'}
 Save 'primary-before.json' $before
 $stage='private package installation'
 # Existing installer creates a real installed layout only inside this fresh
 # preparation root. No primary pointer, shortcuts or game files are changed.
 & (Join-Path $repo 'launcher/Install-BC2VRLauncher.ps1') -InstallRoot $privateRoot -PackageZip $zip -ManifestPath (Join-Path $packaging 'bc2vr-update.json') -GamePath 'G:\New folder (2)\Battlefield Bad Company 2' -NoShortcut *> (Join-Path $run 'private-install.log')
 $privateLock=Enter-FvrLock $privateRoot
 $packageRoot=Join-Path $privateRoot 'versions/0.1.16'
 $package=Test-FvrPackage $packageRoot '0.1.16'
 $runtime=Join-Path $packageRoot 'runtime'
 . (Join-Path $runtime 'Runtime.Core.ps1')
 . (Join-Path $runtime 'Startup.ps1')
 . (Join-Path $runtime 'Dependencies.ps1')
 $config=Get-Bc2RuntimeConfig
 if($config.version -cne '0.1.16' -or $config.asset_recipe_sha256 -cne $recipeHash -or $config.frozen_receipt_sha256 -cne 'a196a0b16622fb85216bad8a9bfdf7598fadea37db20dc050284e0b456004fe4'){throw 'Frozen runtime identity differs'}
 $state=Join-Path $privateRoot 'data/runtime'
 $null=[IO.Directory]::CreateDirectory($state)
 $settings=Read-FvrJson (Join-Path $privateRoot 'data/settings.json')
 $stage='game and placement admission'
 $game=Test-Bc2GameInstallation $settings $config $state
 $placement=Get-FvrLauncherGamePlacement $privateRoot $settings.game_path
 if(-not $placement.allowed){throw $placement.error}
 Save 'placement.json' $placement
 $python=Join-Path $runtime 'python/python.exe'
 $versionChild=Start-Bc2Child $python @('-B','-c','import sys; sys.exit(0 if sys.version_info >= (3,11) else 1)') $run 'python-version'
 if(-not $versionChild.WaitForExit(15000)){throw 'Packaged Python version probe timed out'}
 if($versionChild.ExitCode -ne 0){throw 'Packaged Python version refused'}
 Assert-Bc2RuntimeSpace $state
 $cacheRoot=Get-FvrChild $state 'cache';$cache=Get-FvrChild $cacheRoot $recipeHash
 if(Test-Path -LiteralPath $cache){throw 'Preparation requires a fresh candidate cache'}
 $stage='existing decoder verification'
 $decoderRecipe=Get-Bc2FfmpegRecipe (Join-Path $runtime 'config/ffmpeg-download.json')
 $decoderRoot=Join-Path $primary ('data/runtime/tools/ffmpeg/'+$decoderRecipe.sha256)
 if(-not(Test-Bc2FfmpegCache $decoderRoot $decoderRecipe)){throw 'Existing decoder failed candidate pins; no download attempted'}
 $ffmpeg=Join-Path $decoderRoot 'ffmpeg.exe'
 $stage='candidate assets'
 Lease;Assert-FvrIdle
 $arguments=@('-B',(Join-Path $runtime 'tools/prepare_runtime_assets.py'),'--game',$settings.game_path,'--output',$cache,'--managed-cache-root',$cacheRoot,'--ffmpeg',$ffmpeg,'--recipe',$recipeHash,'--game-sha256',$game.sha256)
 $child=Start-Bc2Child $python $arguments $run 'prepare' @{PYTHONDONTWRITEBYTECODE='1'}
 Save 'child.json' @{pid=$child.Id;creation_filetime=[string]$child.StartTime.ToUniversalTime().ToFileTimeUtc();path=$python;arguments=$arguments;deadline_seconds=300;game_started=$false}
 if(-not $child.WaitForExit(300000)){
  # Only this retained preparation process and its descendants are terminated.
  $child.Kill($true);if(-not $child.WaitForExit(10000)){throw 'Owned preparation process retirement incomplete'}
  throw 'Candidate preparation exceeded five-minute bound'
 }
 $exitCode=$child.ExitCode
 $valid=Test-Bc2AssetCache $cache $recipeHash $game.sha256
 $result=Save-Bc2AssetPreparationResult $cache $state $recipeHash $game.sha256 $exitCode $valid
 if($exitCode -ne 0 -or -not $valid -or $result.state -cne 'complete'){throw ('Candidate assets failed: '+$result.error)}
 $stage='final verification'
 $null=Test-FvrPackage $packageRoot '0.1.16'
 $null=Get-Bc2RuntimeConfig
 $assets=Read-FvrJson (Join-Path $cache 'assets.json')
 $files=@(foreach($file in $assets.files){$path=Get-FvrChild $cache $file.path;if((Get-Item -LiteralPath $path).Length -ne $file.bytes){throw 'Cache length differs'};@{path=$file.path;bytes=$file.bytes;sha256=(Sha $path)}})
 foreach($name in $before.Keys){if((Sha (Join-Path $primary $name)) -cne $before[$name]){throw 'Primary launcher state changed'}}
 Save 'receipt.json' @{schema='fvr.coordinator.prepared-candidate.v1';prepared=$true;version='0.1.16';zip_sha256=$zipHash;runtime_source_sha256=$config.source_sha256;recipe_sha256=$recipeHash;game_sha256=$game.sha256;private_preparation_root=$privateRoot;cache=$cache;files=$files;file_bytes=($files|Measure-Object bytes -Sum).Sum;exit_code=$exitCode;cache_verified=$valid;primary_before=$before;primary_unchanged=$true;primary_installed_version='0.1.12';primary_candidate_activated=$false;game_started=$false;xr_started=$false;native_acceptance=$false;headset_acceptance=$false;redistributable=$false;utc=[DateTime]::UtcNow.ToString('o')}
 @{prepared=$true;cache=$cache;bytes=($files|Measure-Object bytes -Sum).Sum;primary_unchanged=$true;game_started=$false}|ConvertTo-Json
}catch{
 Save 'failure.json' @{stage=$stage;error=$_.Exception.Message;prepared=$false;game_started=$false;xr_started=$false;utc=[DateTime]::UtcNow.ToString('o')}
 throw
}finally{
 $cleanupErrors=[Collections.Generic.List[string]]::new()
 $retired=[Collections.Generic.List[object]]::new();$after=@{};$primaryUnchanged=$null
 try{
  foreach($owned in @($versionChild,$child)){
   if($null -eq $owned){continue}
   $terminated=$false
   try{
    if(-not $owned.HasExited){$owned.Kill($true);$terminated=$true;if(-not $owned.WaitForExit(10000)){throw 'Owned preparation process retirement incomplete'}}
    $retired.Add(@{pid=$owned.Id;creation_filetime=[string]$owned.StartTime.ToUniversalTime().ToFileTimeUtc();exited=$owned.HasExited;terminated_on_exception=$terminated})
   }catch{$cleanupErrors.Add($_.Exception.Message)}
   finally{$owned.Dispose()}
  }
  try{
   foreach($name in $before.Keys){$after[$name]=Sha (Join-Path $primary $name)}
   if($before.Count -eq 2){$primaryUnchanged=($after['active.json'] -ceq $before['active.json'] -and $after['data/settings.json'] -ceq $before['data/settings.json']);if(-not $primaryUnchanged){throw 'Primary launcher state changed'}}
  }catch{$cleanupErrors.Add($_.Exception.Message)}
 }
 finally{
  if($privateLock){$privateLock.Dispose()}
  $primaryLock.Dispose()
 }
 Save 'cleanup.json' @{owned_children=@($retired);errors=@($cleanupErrors);primary_after=$after;primary_unchanged=$primaryUnchanged;launcher_locks_disposed=$true;game_started=$false;utc=[DateTime]::UtcNow.ToString('o')}
 if($cleanupErrors.Count){throw ($cleanupErrors -join '; ')}
}

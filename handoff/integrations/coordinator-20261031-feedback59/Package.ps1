[CmdletBinding()]
param([ValidateSet('Prepare','Install')][string]$Mode='Prepare')
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'));$run=Join-Path $repo 'reports/coordinator-20261031-feedback59'
$primary='G:\Unity\VRMMO\BC2VR-Launcher';$version='0.1.23'
$lease=Get-Content -LiteralPath (Join-Path $run 'leases.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Lease not owned'}
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
function Sha($p){(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant()}
function Save($name,$value){$p=Join-Path $run $name;if(Test-Path -LiteralPath $p){throw "Preserve $p"};[IO.File]::WriteAllText($p,($value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
Assert-FvrIdle
$runtime=Join-Path $run 'runtime';$package=Join-Path $run 'package'
if($Mode -eq 'Prepare'){
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes 512MB -AdditionalRoots @($primary,'G:/Unity/VRMMO/bc2vr-task-worktrees')
 Save 'package-storage.json' $budget
 $receipt=Join-Path $run 'normal-runtime/build-receipt.json'
 $python='G:\Unity\VRMMO\BC2VR-Launcher\versions\0.1.22\runtime\python\python.exe'
 & (Join-Path $repo 'tools/Prepare-DesktopRuntime.ps1') -BuildReceipt $receipt -ReceiptSha256 (Sha $receipt) -OutputPath $runtime -Version $version -Python $python -DependencyManifest 'G:\Unity\VRMMO\bc2vr-recovery\overnight426\public-dependencies426\dependencies-python.json' *> (Join-Path $run 'stage.log')
 if($LASTEXITCODE){throw 'Runtime staging failed'}
 & (Join-Path $repo 'tools/Build-DesktopLauncher.ps1') -Version $version -OutputPath $package -RuntimeRoot $runtime -RuntimeFiles (Join-Path $run 'runtime-files.json') *> (Join-Path $run 'package.log')
 $manifest=Read-FvrJson (Join-Path $package 'bc2vr-update.json');$zip=Join-Path $package $manifest.asset
 $verified=Join-Path $run 'verified-package';Expand-FvrPackage $zip $verified $manifest.unpacked_bytes
 $verifiedManifest=Test-FvrPackage $verified $version
 $config=Read-FvrJson (Join-Path $verified 'runtime/runtime.json')
 foreach($file in $config.files){if((Sha (Get-FvrChild (Join-Path $verified 'runtime') $file.path)) -ne $file.sha256){throw 'Runtime file differs'}}
 $recipe=$config.asset_recipe_sha256;$gameHash=$config.game_sha256[0]
 $oldRecipe=(Read-FvrJson (Join-Path $primary 'versions/0.1.22/runtime/runtime.json')).asset_recipe_sha256
 $oldCache=Get-FvrChild $primary ('data/runtime/cache/'+$oldRecipe)
 if(-not(Test-Bc2AssetCache $oldCache $oldRecipe $gameHash)){throw 'Existing audio cache differs'}
 $completion=Read-FvrJson (Join-Path $repo 'handoff/integrations/coordinator-20261030-wiring54/receipt.json')
 $body=Join-Path 'G:\Unity\VRMMO\bc2vr-task-worktrees\campaign-coordinator-20261028-r1' $completion.cache.path
 if((Sha $body) -ne $completion.cache.sha256 -or (Get-Item $body).Length -ne $completion.cache.bytes){throw 'Completed body cache differs'}
 $lock=Enter-FvrLock $primary
 try {
  Assert-FvrIdle;$before=Sha (Join-Path $primary 'data/settings.json')
  $cacheRoot=Get-FvrChild $primary 'data/runtime/cache';$stage=Get-FvrChild $cacheRoot 'staging-feedback59';$target=Get-FvrChild $cacheRoot $recipe
  if(Test-Path $stage){throw 'Preserve existing staging destination'}
  if(Test-Path $target){
   if(-not(Test-Bc2AssetCache $target $recipe $gameHash) -or (Sha (Join-Path $target 'body-ammo-assets.fvrprop')) -ne (Sha $body)){throw 'Existing matching-recipe cache differs'}
   $files=@((Read-FvrJson (Join-Path $target 'assets.json')).files)
  } else {
  $old=Read-FvrJson (Join-Path $oldCache 'assets.json');$files=@()
  foreach($file in $old.files){
   $source=if($file.path -eq 'body-ammo-assets.fvrprop'){$body}else{Get-FvrChild $oldCache $file.path}
   $dest=Get-FvrChild $stage $file.path;$null=[IO.Directory]::CreateDirectory((Split-Path $dest -Parent));Copy-Item -LiteralPath $source -Destination $dest
   if((Sha $dest) -ne (Sha $source)){throw 'Cache copy differs'}
   $files+=@{path=$file.path;sha256=Sha $dest;bytes=(Get-Item $dest).Length}
  }
  if(($files|Measure-Object bytes -Sum).Sum -gt 64MB){throw 'Prepared cache exceeds bound'}
  Write-FvrJson (Join-Path $stage 'assets.json') @{schema='bc2vr.local-assets.v1';recipe_sha256=$recipe;game_sha256=$gameHash;files=$files;redistributable=$false}
  if(-not(Test-Bc2AssetCache $stage $recipe $gameHash)){throw 'Staged cache rejected'}
  foreach($path in @($stage,$target)){if([IO.Path]::GetFullPath((Split-Path $path -Parent)) -ine [IO.Path]::GetFullPath($cacheRoot)){throw 'Cache move escaped managed root'}}
  [IO.Directory]::Move($stage,$target)
  }
  if(-not(Test-Bc2AssetCache $target $recipe $gameHash) -or (Sha (Join-Path $primary 'data/settings.json')) -ne $before){throw 'Final cache/settings verification failed'}
  Save 'package-verification.json' @{version=$version;zip_sha256=Sha $zip;package_files=$verifiedManifest.files.Count;runtime_files=$config.files.Count;recipe_sha256=$recipe;cache=$target;cache_files=$files;settings_sha256=$before;source_sha256=$config.source_sha256;installed=$false;game_started=$false;headset_accepted=$false}
 } finally {$lock.Dispose()}
 Write-Output 'Private0.1.23 payload and264-record local cache prepared and verified.'
 return
}
$verification=Read-FvrJson (Join-Path $run 'package-verification.json')
$pushed=Read-FvrJson (Join-Path $run 'source-push.json')
if(-not $pushed.pushed){throw 'Source push must be verified before installation'}
$manifest=Read-FvrJson (Join-Path $package 'bc2vr-update.json');$zip=Join-Path $package $manifest.asset
if((Sha $zip) -ne $verification.zip_sha256){throw 'Prepared package changed'}
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $primary -Operation Build -PlannedBytes 256MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees')
Save 'install-storage.json' $budget
$lock=Enter-FvrLock $primary
try {
 Assert-FvrIdle;$settings=Sha (Join-Path $primary 'data/settings.json');$active=Read-FvrJson (Join-Path $primary 'active.json')
 if($active.current -ne '0.1.22' -or $settings -ne $verification.settings_sha256){throw 'Installed baseline changed'}
 Install-FvrPackage $primary $zip $manifest
 $current=Read-FvrJson (Join-Path $primary 'active.json');$installed=Test-FvrPackage (Join-Path $primary ('versions/'+$version)) $version
 if($current.current -ne $version -or $current.previous -ne '0.1.22' -or (Sha (Join-Path $primary 'data/settings.json')) -ne $settings){throw 'Installation pointer/settings differ'}
 Save 'installation.json' @{installed=$true;version=$version;previous=$current.previous;settings_preserved=$true;settings_sha256=$settings;package_files=$installed.files.Count;cache_verified=$true;source_sha256=$verification.source_sha256;game_started=$false;headset_accepted=$false}
} finally {$lock.Dispose()}
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File (Join-Path $primary ('versions/'+$version+'/app/Worker.ps1')) -InstallRoot $primary -Action Validate *> (Join-Path $run 'installed-validation.log')
if($LASTEXITCODE){
 $lock=Enter-FvrLock $primary
 try {Assert-FvrIdle;Switch-FvrPrevious $primary;Save 'installation-rollback.json' @{restored='0.1.22';validation_failed=$true;installed_candidate=$false;game_started=$false}}
 finally {$lock.Dispose()}
 throw 'Installed validation failed; restored0.1.22'
}
Get-Content (Join-Path $run 'installed-validation.log') -Tail 12

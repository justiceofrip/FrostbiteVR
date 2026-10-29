#requires -Version 7.5
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$run=Join-Path $repo 'reports/coordinator-20261029-prep07/run01'
$primary='G:\Unity\VRMMO\BC2VR-Launcher'
$recipe='f31c3f3c1270f807f41b6947c2ffdb378528d0199c94e74c81212d39cbd09983'
$gameHash='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
$source=Join-Path $run ('preparation-launcher/data/runtime/cache/'+$recipe)
$cacheRoot=Join-Path $primary 'data/runtime/cache'
$target=Join-Path $cacheRoot $recipe
$stage=Join-Path $cacheRoot 'staging-coordinator-prep07'
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save($name,$value){
 $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value|ConvertTo-Json -Depth 10))
 $stream=[IO.File]::Open((Join-Path $run $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Lease {
 $lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json
 if($lease.owner_token -cne 'campaign-coordinator-prep07-b68ed7bd465344e48ba49a89a4dc4ce0'){throw 'Integration lease differs'}
}
Lease
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
if((Sha (Join-Path $run 'receipt.json')) -cne '91ee02ed341f71101e6fc8fc645fcf5e7cf661e85c57baf9b38161fc1698b25c' -or (Sha (Join-Path $run 'cleanup.json')) -cne '1bf995b7ffe22e2f545642e495ce55464bdb15408e17eacda9b1d1b6b6e62700'){throw 'Root-verified preparation evidence changed'}
$receipt=Read-FvrJson (Join-Path $run 'receipt.json')
$cleanup=Read-FvrJson (Join-Path $run 'cleanup.json')
if(-not $receipt.prepared -or -not $receipt.cache_verified -or $receipt.exit_code -ne 0 -or $receipt.cache -cne $source -or $receipt.recipe_sha256 -cne $recipe -or $receipt.game_sha256 -cne $gameHash -or $cleanup.errors.Count -or -not $cleanup.primary_unchanged -or @($cleanup.owned_children|Where-Object {-not $_.exited}).Count){throw 'Preparation/retirement receipt refused'}
$lock=Enter-FvrLock $primary
$before=@{};$moved=$false
try {
 Assert-FvrIdle
 foreach($path in $source,$cacheRoot,$target,$stage){Assert-FvrNoLinks $path}
 # Both sides of the atomic directory move stay in the exact managed root.
 foreach($path in $target,$stage){if([IO.Path]::GetFullPath((Split-Path $path -Parent)) -cne [IO.Path]::GetFullPath($cacheRoot)){throw 'Cache move escaped intended root'}}
 if((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $stage)){throw 'Preserve existing candidate/staging cache'}
 foreach($name in 'active.json','data/settings.json'){$before[$name]=Sha (Join-Path $primary $name);if($before[$name] -cne $receipt.primary_before[$name]){throw 'Primary baseline changed'}}
 $assets=Read-FvrJson (Join-Path $source 'assets.json')
 $expected=@('body-ammo-assets.fvrprop','pump/spas-rear.wav','pump/spas-closure.wav','pump/mechanism-audio.json','shell/spas-shell-insert.wav','shell/shell-reload-audio.json')
 if($assets.files.Count -ne 6 -or @($assets.files.path|Sort-Object -Unique).Count -ne 6 -or @($assets.files|Where-Object {$_.path -cnotin $expected}).Count -or $assets.redistributable -ne $false -or -not(Test-Bc2AssetCache $source $recipe $gameHash)){throw 'Source cache contract differs'}
 foreach($file in $assets.files){
  $pin=@($receipt.files|Where-Object {$_.path -ceq $file.path})
  if($pin.Count -ne 1 -or $pin[0].sha256 -cne $file.sha256 -or $pin[0].bytes -ne $file.bytes){throw 'Source files differ from completed preparation'}
 }
 if(($assets.files|Measure-Object bytes -Sum).Sum -ne 16854370 -or ($assets.files|Where-Object path -CEQ 'body-ammo-assets.fvrprop').bytes -ne 16750541){throw 'Prepared output sizes differ from verified result'}
 $copy=@(foreach($file in $assets.files){$path=Get-FvrChild $source $file.path;$length=(Get-Item -LiteralPath $path).Length;if($length -ne $file.bytes -or $length -le 0){throw 'Source length differs'};@{path=$file.path;bytes=$length;sha256=$file.sha256}})
 if(($copy|Measure-Object bytes -Sum).Sum -gt 32MB){throw 'Source exceeds derived cache bound'}
 foreach($name in 'assets.json','preparation.json'){$path=Get-FvrChild $source $name;Assert-FvrNoLinks $path;$length=(Get-Item -LiteralPath $path).Length;if($length -gt 64KB){throw 'Unbounded cache metadata'};$copy+=@{path=$name;bytes=$length;sha256=(Sha $path)}}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $stage -Operation Session -PlannedBytes 64MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 Save 'storage-stage-cache.json' $budget
 [IO.Directory]::CreateDirectory($stage)|Out-Null
 foreach($name in 'pump','shell'){[IO.Directory]::CreateDirectory((Get-FvrChild $stage $name))|Out-Null}
 foreach($file in $copy){
  $from=Get-FvrChild $source $file.path;$to=Get-FvrChild $stage $file.path
  Assert-FvrNoLinks $from;Assert-FvrNoLinks $to
  Copy-Item -LiteralPath $from -Destination $to -ErrorAction Stop
  if((Get-Item -LiteralPath $to).Length -ne $file.bytes -or (Sha $to) -cne $file.sha256){throw 'Staged cache copy differs'}
 }
 if(-not(Test-Bc2AssetCache $stage $recipe $gameHash)){throw 'Staged cache not admitted'}
 Lease;Assert-FvrIdle;Assert-FvrNoLinks $stage;Assert-FvrNoLinks $target
 if(Test-Path -LiteralPath $target){throw 'Candidate cache appeared before publication'}
 [IO.Directory]::Move($stage,$target);$moved=$true
 if(-not(Test-Bc2AssetCache $target $recipe $gameHash)){throw 'Final candidate cache not admitted'}
 foreach($name in $before.Keys){if((Sha (Join-Path $primary $name)) -cne $before[$name]){throw 'Primary pointer/settings changed'}}
 Save 'cache-stage.json' @{schema='fvr.coordinator.candidate-cache-stage.v1';staged=$true;source_receipt_sha256=(Sha (Join-Path $run 'receipt.json'));recipe_sha256=$recipe;game_sha256=$gameHash;target=$target;files=$copy;cache_verified=$true;primary_before=$before;primary_unchanged=$true;active_version='0.1.12';candidate_activated=$false;game_started=$false;xr_started=$false;redistributable=$false;utc=[DateTime]::UtcNow.ToString('o')}
 @{cache_staged=$true;target=$target;candidate_activated=$false;active_version='0.1.12'}|ConvertTo-Json
}catch{
 Save 'cache-stage-failure.json' @{error=$_.Exception.Message;target_published=$moved;utc=[DateTime]::UtcNow.ToString('o')}
 throw
}finally{$lock.Dispose()}

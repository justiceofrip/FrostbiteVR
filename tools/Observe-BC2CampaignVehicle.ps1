#requires -Version 7.0
[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Recipe,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$RecipeSha256,
 [Parameter(Mandatory)][string]$Case,
 [Parameter(Mandatory)][string]$GameReceipt,
 [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$GameReceiptSha256,
 [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=Split-Path $PSScriptRoot -Parent
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function ReadJson($path){if((Get-Item -LiteralPath $path).Length -gt 8MB){throw 'Oversized JSON'};Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable}
function Check($pin){if((Get-Item -LiteralPath $pin.path).Length -ne $pin.bytes -or (Sha $pin.path) -cne $pin.sha256){throw ('Dependency differs: '+$pin.path)}}
if((Sha $Recipe) -cne $RecipeSha256 -or (Sha $GameReceipt) -cne $GameReceiptSha256){throw 'Recipe/process receipt changed'}
$r=ReadJson $Recipe;$game=ReadJson $GameReceipt
if($r.schema -cne 'fvr.bc2.campaign-vehicle-observation-recipe.v1' -or -not $r.read_only -or $r.game_actions -ne 0){throw 'Read-only recipe required'}
foreach($pin in $r.dependencies){Check $pin}
foreach($pin in @($r.capture,$r.capture_build,$r.python,$r.map_reader,$r.catalog,$r.audit,$r.observer)){Check $pin}
if($r.observer.path -cne $PSCommandPath){throw 'Observer not recipe-bound'}
$catalog=ReadJson $r.catalog.path;$cases=@($catalog.cases|Where-Object id -CEQ $Case)
if($cases.Count -ne 1){throw 'Exact catalog case required'};$scenario=$cases[0]
$captureBuild=ReadJson $r.capture_build.path
if($captureBuild.binary.path -cne $r.capture.path -or $captureBuild.binary.sha256 -cne $r.capture.sha256){throw 'Capture binary/build identity differs'}
if(-not [IO.Path]::IsPathFullyQualified($Output) -or (Test-Path -LiteralPath $Output)){throw 'New absolute output required'}
$outPath=[IO.Path]::GetFullPath($Output)
$allowedRoot=[IO.Path]::GetFullPath($r.output_root).TrimEnd('\')+'\'
if(-not $outPath.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Output outside prepared recovery root'}
if(-not (Test-Path -LiteralPath (Split-Path $outPath -Parent) -PathType Container)){throw 'Output parent must exist'}
function Assert-Game {
 $target=Get-Process -Id $game.pid -ErrorAction Stop
 if($target.HasExited -or $target.Path -cne $game.path -or $target.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -cne [string]$game.creation_filetime){throw 'Exact game lifetime differs'}
 if((Sha $game.path) -cne $r.game_sha256){throw 'Game executable differs'}
}
Assert-Game
. (Join-Path $PSScriptRoot 'StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $outPath -Operation Capture -PlannedBytes 64MB -RawCaptureBytes 0
[IO.Directory]::CreateDirectory($outPath)|Out-Null
$failure=$null;$passed=$false;$captureExit=$null
function Read-Map($name){
 Assert-Game
 & $r.python.path -B $r.map_reader.path --pid $game.pid --creation $game.creation_filetime --out (Join-Path $outPath ($name+'.json')) *> (Join-Path $outPath ($name+'.log'))
 if($LASTEXITCODE){throw ('Native loaded-level observation failed: '+$name)}
 $map=ReadJson (Join-Path $outPath ($name+'.json'))
 if(-not $map.passed -or $map.pid -ne $game.pid -or [string]$map.creation_filetime -cne [string]$game.creation_filetime -or $map.client_level_setting_used -or @($map.snapshots).Count -ne 3){throw 'Loaded-level evidence incomplete'}
 foreach($sample in $map.snapshots){if(-not $sample.coherent -or $sample.native_mapname -cne $scenario.level){throw 'Wrong actual loaded level; no seat capture started'}}
 return $map
}
try {
 $budget|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $outPath 'storage.json') -Encoding utf8
 $null=Read-Map 'map-before'
 Assert-Game
 & $r.capture.path $game.pid $game.creation_filetime $game.path (Join-Path $outPath 'seat') *> (Join-Path $outPath 'seat.log')
 $captureExit=$LASTEXITCODE
 if($captureExit){throw 'Bounded seat collection incomplete; preserve its failure, do not retry'}
 $null=Read-Map 'map-after'
 & $r.python.path -B $r.audit.path --catalog $r.catalog.path --capture ($Case+'='+(Join-Path $outPath 'seat/capture.json')) --output (Join-Path $outPath 'coverage.json') *> (Join-Path $outPath 'audit.log')
 if($LASTEXITCODE){throw 'Offline seat coherence audit refused; preserve raw evidence'}
 Assert-Game;$passed=$true
} catch {$failure=$_.Exception.Message}
finally {
 [ordered]@{schema='fvr.bc2.campaign-vehicle-observation.v1';collected=$passed;failure=$failure;
  scenario_requested=$Case;expected_level=$scenario.level;requested_asset_not_native_proof=$scenario.asset;
  recipe_sha256=$RecipeSha256;game_receipt_sha256=$GameReceiptSha256;pid=$game.pid;creation_filetime=$game.creation_filetime;
  capture_exit=$captureExit;game_writes=0;native_calls=0;os_input=0;profile_changes=0;game_closed=$false;
  exact_blueprint_verified=$false;campaign_accepted=$false;controls_tested=$false;headset_tested=$false
 }|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $outPath 'completion.json') -Encoding utf8
}
if(-not $passed){throw $failure}
Write-Output 'Read-only seat evidence collected. Exact blueprint, control response and campaign acceptance remain unproven.'

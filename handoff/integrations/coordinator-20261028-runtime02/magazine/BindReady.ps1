#requires -Version 7.5
# Reviewed successor binder; preserves the original failed binding attempt.
param([Parameter(Mandatory)][string]$BuildReceipt,[Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$Sha256)
$ErrorActionPreference='Stop';$repo='G:\Unity\VRMMO\FrostbiteVR'
. (Join-Path $PSScriptRoot 'CommonReviewed.ps1');$prep=Assert-Preparation
$b=Assert-CompletedBuild $BuildReceipt $Sha256
$admission=Join-Path $EvidenceRoot 'admission-inputs.json';$binding=Join-Path $EvidenceRoot 'runtime-binding.json'
if((Test-Path -LiteralPath $admission) -or (Test-Path -LiteralPath $binding)){throw 'Preserve existing binding'}
$v=Load (Join-Path $RecipeRoot 'admission-template.json');$v.compiled_source_sha256=$b.source_sha256
Assert-Lease
[IO.File]::WriteAllText($admission,($v|ConvertTo-Json -Depth 24),[Text.UTF8Encoding]::new($false))
$r=@{schema='fvr.runtime02.magazine.runtime-binding.v1';build=(Pin $BuildReceipt);admission=(Pin $admission);source=(Pin (Join-Path $RecipeRoot 'source-preparation.json'));workflow=(Pin (Join-Path $RecipeRoot 'workflow-preparation.json'));operation_header_review=(Pin (Join-Path $RecipeRoot 'operation-header-review.json'));actual_runtime_accepted=$false;game_actions=0;scope='Nambu magazine only; not CHAT07 combined interactions or installed-runtime acceptance'}
Assert-Lease
[IO.File]::WriteAllText($binding,($r|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false));Pin $binding|ConvertTo-Json

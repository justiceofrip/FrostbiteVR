[CmdletBinding()]
param([string]$Python='python.exe',[string]$GamePath,[string]$Output)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if(-not $GamePath){$local=Get-Content -LiteralPath (Join-Path $repoRoot 'config\local.json') -Raw | ConvertFrom-Json;$GamePath=$local.game_path}
if(-not $Output){$Output=Join-Path $repoRoot 'build\body-ammo-assets.fvrprop'}
# This preparation only reads installed archives. It never opens a game process,
# changes the installation, initializes a graphics device, or enables rendering.
& $Python -B (Join-Path $PSScriptRoot 'bc2_body_ammo_assets.py') --game $GamePath --output $Output --body-equipment --catalog (Join-Path $repoRoot 'config\body-ammo-assets.json')
if($LASTEXITCODE -ne 0){throw 'Body ammunition geometry preparation failed; native drawing remains unavailable.'}
Write-Output 'Private ammo/equipment cache prepared for the host renderer. In-game visibility needs runtime validation; do not distribute this cache.'

[CmdletBinding()]
param([string]$GamePath='',[ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
if(-not $GamePath){
    $localConfig=Join-Path $PSScriptRoot 'config\local.json'
    if(-not (Test-Path -LiteralPath $localConfig)){throw 'Specify -GamePath or set game_path in config\local.json.'}
    $GamePath=(Get-Content -LiteralPath $localConfig -Raw | ConvertFrom-Json).game_path
    if(-not $GamePath -or -not [IO.Path]::IsPathRooted($GamePath)){throw 'game_path must be an absolute installation folder.'}
}
$exe=Join-Path $PSScriptRoot "build\$Architecture\BC2Inspect.exe"
if(-not (Test-Path -LiteralPath $exe)){throw "Build $Architecture first using Build.ps1."}
$output=& $exe --game $GamePath
$code=$LASTEXITCODE
if($code -ne 0 -and $code -ne 2 -and $code -ne 3){throw "Inspector failed ($code)."}
$report=($output -join [Environment]::NewLine) | ConvertFrom-Json
$game=Join-Path $GamePath 'BFBC2Game.exe'
if($code -eq 0){$report | Add-Member -NotePropertyName executable_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash}
$report | Add-Member -NotePropertyName inspected_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$destination=Join-Path $PSScriptRoot 'reports\bc2-installation.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $destination -Encoding utf8
$report | ConvertTo-Json -Depth 8
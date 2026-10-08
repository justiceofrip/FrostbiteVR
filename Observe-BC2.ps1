[CmdletBinding()]
param([string]$GamePath='')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
if(-not $GamePath){$GamePath=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json).game_path}
if(-not $GamePath -or -not [IO.Path]::IsPathRooted($GamePath)){throw 'Specify an absolute -GamePath.'}
$exe=Join-Path $PSScriptRoot 'build\x64\BC2Observe.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Build x64 first using Build.ps1.'}
$output=& $exe --game $GamePath
$code=$LASTEXITCODE
if($code -ne 0 -and $code -ne 2){throw "Read-only observation failed ($code)."}
$report=($output -join [Environment]::NewLine) | ConvertFrom-Json
$report | Add-Member -NotePropertyName observed_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$report | Add-Member -NotePropertyName game_path -NotePropertyValue $GamePath
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'reports\bc2-native-observation.json') -Encoding utf8
$report | ConvertTo-Json -Depth 6
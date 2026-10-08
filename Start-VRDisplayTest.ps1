[CmdletBinding()]
param([ValidateRange(1,300)][int]$Seconds=60)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$exe=Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe'
if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Build x64 first.'}
$folder=Join-Path $PSScriptRoot ('reports\xr-display-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
Write-Host 'Starting the labeled BC2 VR DISPLAY TEST. This is a synthetic stereo room, not BC2 gameplay.'
Write-Host 'Connect the headset to the PC runtime before starting this command.'
& $exe --test-scene --seconds $Seconds 1> (Join-Path $folder 'host.json') 2> (Join-Path $folder 'runtime.stderr.log')
$hostExit=$LASTEXITCODE
$result=Get-Content -LiteralPath (Join-Path $folder 'host.json') -Raw | ConvertFrom-Json
$result | Add-Member -NotePropertyName tested_binary_sha256 -NotePropertyValue ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)
$result | Add-Member -NotePropertyName completed_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'host.json') -Encoding utf8
$result | ConvertTo-Json -Depth 5
if($hostExit -ne 0){Get-Content -LiteralPath (Join-Path $folder 'runtime.stderr.log') -Tail 12;throw 'OpenXR display test did not complete. See the report above.'}
if($result.submitted_pairs -eq 0){Write-Warning 'Session ended without submitting an image pair. Headset display has not been verified.'}

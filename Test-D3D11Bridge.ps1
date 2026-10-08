[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$producer=Join-Path $PSScriptRoot 'build\x86\BC2BridgeProbe.exe'
$consumer=Join-Path $PSScriptRoot 'build\x64\BC2BridgeProbe.exe'
foreach($path in @($producer,$consumer)){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Build x86 and x64 first: missing $path"}}
$output=& $producer --consumer-exe $consumer
if($LASTEXITCODE -ne 0){throw "D3D11 bridge acceptance failed ($LASTEXITCODE)."}
$result=$output | ConvertFrom-Json
if($result.state -ne 'verified' -or $result.producer_bits -ne 32 -or $result.consumer_bits -ne 64 -or -not $result.both_eye_pixels_match){throw 'Unexpected bridge probe result.'}
$report=[ordered]@{
 completed_utc=[DateTime]::UtcNow.ToString('o')
 producer_sha256=(Get-FileHash -LiteralPath $producer -Algorithm SHA256).Hash
 consumer_sha256=(Get-FileHash -LiteralPath $consumer -Algorithm SHA256).Hash
 result=$result
}
$reports=Join-Path $PSScriptRoot 'reports'
New-Item -ItemType Directory -Path $reports -Force | Out-Null
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reports 'd3d11-cross-architecture.json')
$result | ConvertTo-Json

[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$WorkRoot,
      [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')][string]$Version,
      [Parameter(Mandatory=$true)][string]$LoaderPath,
      [string]$SourceRoot=(Split-Path -Parent $PSScriptRoot),[string]$OverlayRoot,
      [string]$Python='python.exe',[ValidateRange(1,16)][int]$Jobs=4)
$ErrorActionPreference='Stop'
$work=[IO.Path]::GetFullPath($WorkRoot)
if(Test-Path -LiteralPath $work){throw 'WorkRoot must be a new, nonexistent directory.'}
$source=[IO.Path]::GetFullPath($SourceRoot)
if($source -eq $work -or $source.StartsWith($work.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'WorkRoot cannot contain the source tree.'}
$loader=[IO.Path]::GetFullPath($LoaderPath)
$loaderHash=(Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash.ToLowerInvariant()
if($loaderHash -ne 'a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f'){throw 'LoaderPath does not match the pinned Khronos loader.'}
$packager=Join-Path $PSScriptRoot 'package_preview.py'
$arguments=@($packager,'--root',$source,'--out',(Join-Path $work 'snapshot'),'--version',$Version,'--source-only')
if($OverlayRoot){$arguments+=@('--overlay',[IO.Path]::GetFullPath($OverlayRoot))}
& $Python @arguments
if($LASTEXITCODE -ne 0){throw 'Source snapshot failed.'}
$snapshot=Join-Path $work 'snapshot\source'
$loaderDestination=Join-Path $snapshot 'runtime\openxr\win64\openxr_loader.dll'
New-Item -ItemType Directory -Path (Split-Path -Parent $loaderDestination) -Force | Out-Null
Copy-Item -LiteralPath $loader -Destination $loaderDestination -ErrorAction Stop
$counts=[ordered]@{}
$previousCl=$env:_CL_
try {
    # Embed debug information per object, avoiding a shared compiler PDB server.
    $env:_CL_=($previousCl+' /Z7').Trim()
    foreach($architecture in @('x86','x64')){
        & (Join-Path $snapshot 'Build.ps1') -Architecture $architecture -Jobs $Jobs
        $build=Get-Content -LiteralPath (Join-Path $snapshot ('reports\'+$architecture+'-build.json')) -Raw | ConvertFrom-Json
        $log=Get-Content -LiteralPath (Join-Path $snapshot ('reports\'+$architecture+'-tests.log')) -Raw
        $match=[regex]::Match($log,'100% tests passed, 0 tests failed out of (\d+)')
        if(-not $build.built -or -not $build.tests_passed -or -not $match.Success){throw "No complete passing suite for $architecture"}
        $counts[$architecture]=[int]$match.Groups[1].Value
    }
} finally {$env:_CL_=$previousCl}
$hashes=[ordered]@{}
foreach($relative in @('build/x86/BC2NativeProbe.dll','build/x86/BC2NativeTrace.exe','build/x64/BC2XrHost.exe','build/x64/runtime/openxr/win64/openxr_loader.dll')){
    $hashes[$relative]=(Get-FileHash -LiteralPath (Join-Path $snapshot $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$evidence=[ordered]@{schema=1;source_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $snapshot 'source-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();test_counts=$counts;binary_sha256=$hashes}
$attestation=Join-Path $work 'build-attestation.json'
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $attestation -Encoding utf8
& $Python (Join-Path $snapshot 'tools\package_preview.py') --root $snapshot --out (Join-Path $work 'artifacts') --version $Version --build-attestation $attestation --require-build-verified
if($LASTEXITCODE -ne 0){throw 'Final package validation failed.'}
Write-Output 'Offline build and package staging completed. Exact-payload native/headset acceptance and publication remain separate.'

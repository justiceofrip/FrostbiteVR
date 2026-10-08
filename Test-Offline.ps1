<#
.SYNOPSIS
Build and verify the framework without starting BC2 or a headset runtime.
.DESCRIPTION
Both architectures run their normal CTest suites. -Gpu additionally checks the
actual OpenXR host with its test-only runtime and both cross-process GPU bridges.
A unique report folder preserves logs and failures from this invocation.
#>
[CmdletBinding()]
param([switch]$Gpu, [ValidateRange(1,16)][int]$Jobs=4)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$folder=Join-Path $PSScriptRoot ('reports\offline-'+$stamp+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $folder -ErrorAction Stop | Out-Null
$checks=[Collections.Generic.List[object]]::new()
$report=[ordered]@{
    schema_version=1
    started_utc=[DateTime]::UtcNow.ToString('o')
    completed_utc=$null
    scope='offline-framework'
    offline_checks_passed=$false
    gpu_requested=[bool]$Gpu
    native_rendering_tested=$false
    headset_tested=$false
    architecture_suites=[ordered]@{}
    checks=$checks
    binary_sha256=[ordered]@{}
    error=$null
}
function Invoke-OfflineCheck([string]$Name,[scriptblock]$Action){
    $log=Join-Path $folder ($Name+'.log')
    $timer=[Diagnostics.Stopwatch]::StartNew()
    try {
        & $Action *> $log
        $checks.Add([ordered]@{name=$Name;status='passed';elapsed_ms=$timer.ElapsedMilliseconds;log=[IO.Path]::GetFileName($log)})
        Write-Host "PASS $Name"
    } catch {
        $checks.Add([ordered]@{name=$Name;status='failed';elapsed_ms=$timer.ElapsedMilliseconds;log=[IO.Path]::GetFileName($log);error=$_.Exception.Message})
        throw
    }
}
try {
    foreach($architecture in @('x86','x64')){
        Invoke-OfflineCheck ('build-'+$architecture) {
            & (Join-Path $PSScriptRoot 'Build.ps1') -Architecture $architecture -Jobs $Jobs
            foreach($name in @('configure.log','build.log','tests.log','build.json')){
                Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('reports\'+$architecture+'-'+$name)) -Destination $folder
            }
            $build=Get-Content -LiteralPath (Join-Path $folder ($architecture+'-build.json')) -Raw | ConvertFrom-Json
            if(-not $build.built -or -not $build.tests_passed){throw "Incomplete $architecture build/test evidence"}
            $testLog=Get-Content -LiteralPath (Join-Path $folder ($architecture+'-tests.log')) -Raw
            $totals=[regex]::Match($testLog,'100% tests passed, 0 tests failed out of (\d+)')
            if(-not $totals.Success -or [int]$totals.Groups[1].Value -lt 1){throw "No passing CTest suites found for $architecture"}
            $report.architecture_suites[$architecture]=[int]$totals.Groups[1].Value
        }
    }
    if($Gpu){
        Invoke-OfflineCheck 'xr-presentation' {
            $fixture=Join-Path $PSScriptRoot 'build\x64\OpenXrPresentationTests.exe'
            $testRuntime=Join-Path $PSScriptRoot 'build\x64\FvrTestOpenXr.dll'
            # Explicit test DLL; this never selects the installed OpenXR runtime.
            & $fixture $testRuntime
            if($LASTEXITCODE -ne 0){throw "OpenXR presentation fixture failed ($LASTEXITCODE)"}
        }
        Invoke-OfflineCheck 'xr-recovery' {
            & (Join-Path $PSScriptRoot 'build\x64\OpenXrRecoveryTests.exe') (Join-Path $PSScriptRoot 'build\x64\FvrTestOpenXr.dll')
            if($LASTEXITCODE -ne 0){throw "OpenXR recovery fixture failed ($LASTEXITCODE)"}
        }
        Invoke-OfflineCheck 'gpu-nt-bridge' {
            & (Join-Path $PSScriptRoot 'Test-FrameChannel.ps1') -FrameBridge
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'reports\ipc-frame-bridge-cross-architecture.json') -Destination $folder
        }
        Invoke-OfflineCheck 'gpu-legacy-bridge' {
            & (Join-Path $PSScriptRoot 'Test-FrameChannel.ps1') -LegacyIpc
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'reports\ipc-legacy-textures-cross-architecture.json') -Destination $folder
        }
        foreach($relative in @('build\x64\OpenXrPresentationTests.exe','build\x64\OpenXrRecoveryTests.exe','build\x64\FvrTestOpenXr.dll','build\x86\BC2IpcProbe.exe','build\x64\BC2IpcProbe.exe')){
            $report.binary_sha256[$relative]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $relative) -Algorithm SHA256).Hash
        }
    } else {
        foreach($name in @('xr-presentation','xr-recovery','gpu-nt-bridge','gpu-legacy-bridge')){
            $checks.Add([ordered]@{name=$name;status='skipped';reason='Enable with -Gpu'})
        }
    }
    $report.offline_checks_passed=$true
} catch {
    $report.error=$_.Exception.Message
    throw
} finally {
    $report.completed_utc=[DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $folder 'summary.json') -Encoding utf8
    Write-Host ('Report: '+(Join-Path $folder 'summary.json'))
}

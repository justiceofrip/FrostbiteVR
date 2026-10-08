[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x86',
      [ValidateRange(1,16)][int]$Jobs=1,[string]$BuildDirectory='',
      [switch]$OrdinaryManualCycles, [switch]$OrdinaryBoltInputProbe,
      [switch]$OrdinaryResourceInputProbe)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$profiles=Join-Path $PSScriptRoot 'profiles/checkpoint202'
$operation=Join-Path $PSScriptRoot 'profiles/checkpoint245'
$receipt=Join-Path $operation 'source-with-header.json'
$proof=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
& (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation | Out-Null
$build=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory,$PSScriptRoot)}else{Join-Path $PSScriptRoot "build/$Architecture"}
$cycleOption=if($OrdinaryManualCycles){'ON'}else{'OFF'}
if(($OrdinaryBoltInputProbe -or $OrdinaryResourceInputProbe) -and -not $OrdinaryManualCycles){throw 'Ordinary input diagnostic requires ordinary manual cycles'}
if($OrdinaryBoltInputProbe -and $OrdinaryResourceInputProbe){throw 'Select one bounded input driver'}
$inputOption=if($OrdinaryBoltInputProbe){'ON'}else{'OFF'}
$resourceInputOption=if($OrdinaryResourceInputProbe){'ON'}else{'OFF'}
& cmake -S $PSScriptRoot -B $build -G Ninja `
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo' '-DBUILD_TESTING=ON' `
    "-DBC2_ORDINARY_MANUAL_CYCLES=$cycleOption" `
    "-DBC2_ORDINARY_BOLT_INPUT_PROBE=$inputOption" `
    "-DBC2_ORDINARY_RESOURCE_INPUT_PROBE=$resourceInputOption" `
    '-DBC2_ARMING_EMPTY_PROBE=OFF' '-DBC2_AMMO_MOVE_PROBE=OFF' '-DBC2_AMMO_REFILL_PROBE=OFF' '-DBC2_AMMO_RESOURCE_HANDS=OFF' '-DBC2_RESOURCE_MAGAZINES=ON' '-DBC2_MANUAL_BOLT_PROBE=OFF' '-DBC2_MANUAL_PUMP_HANDS=OFF' '-DBC2_MANUAL_PUMP_PROBE=OFF' '-DBC2_M95_STOCK_SHOT_PROBE=OFF' '-DBC2_RESOURCE_INVENTORY_PROBE=OFF' '-DBC2_RESOURCE_PUMP_PROBE=OFF' '-DBC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO=0' `
    '-DBC2_PREHOLD_CLEANUP=ON' '-DBC2_AUTHORED_GRIP_ACCEPTED_BASELINES=OFF' `
    '-DBC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR=' "-DBC2_REVIEWED_MAGAZINE_REGISTRY_HEADER=$PSScriptRoot/profiles/resource-enrollment240-f2000/CombinedRegistry.h" '-DBC2_DRAW_CATALOG_HEADER=' `
    "-DBC2_AUTHORED_GRIP_HEADER=$profiles/Bc2AuthoredGrips.generated.h" `
    "-DBC2_AUTHORED_SUPPORT_HEADER=$profiles/Bc2AuthoredSupports.generated.h" `
    "-DBC2_EXPERIMENTAL_MAGAZINE_HEADER=$PSScriptRoot/profiles/resource-enrollment240-f2000/CombinedGeometry.h" `
    "-DBC2_NATIVE_OPERATION_CAPABILITIES_HEADER=$operation/Bc2NativeOperationCapabilities.generated.h" `
    "-DBC2_NATIVE_OPERATION_SOURCE_RECEIPT=$receipt" `
    "-DBC2_NATIVE_OPERATION_SOURCE_SHA256=$($proof.source_sha256)"
if($LASTEXITCODE -ne 0){throw 'Checkpoint configuration or exact source verification failed.'}
& (Join-Path $PSScriptRoot 'Build.ps1') -Architecture $Architecture -Jobs $Jobs -BuildDirectory $build
$capabilityOutput=& (Join-Path $build 'BC2WeaponCapabilityProbe.exe')
if($LASTEXITCODE -ne 0){throw 'Compiled weapon capability report failed'}
$capabilities=($capabilityOutput -join "`n") | ConvertFrom-Json
if($capabilities.schema -ne 'fvr.bc2.compiled-capabilities.v1'){throw 'Unexpected compiled capability schema'}
$capabilityPath=Join-Path $PSScriptRoot "reports/$Architecture-compiled-capabilities.json"
$capabilityOutput | Set-Content -LiteralPath $capabilityPath -Encoding utf8
$magazines=@($capabilities.native_registrations | Where-Object {$_.enabled -and $_.resolved -and $_.reviewed -and $_.magazine_ready})
Write-Host "Compiled magazine prerequisites: $($magazines.Count) exact configurations. Native gameplay and headset acceptance are separate. Report: $capabilityPath"


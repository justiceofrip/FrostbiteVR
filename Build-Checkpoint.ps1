[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x86',
      [ValidateRange(1,16)][int]$Jobs=1,[string]$BuildDirectory='',
      [switch]$OrdinaryManualCycles, [switch]$OrdinaryBoltInputProbe,
      [switch]$OrdinaryResourceInputProbe, [switch]$NativeCycleRecovery,
      [switch]$OrdinaryResourceInterruptionProbe, [switch]$OrdinaryResourceSceneProbe,
      [switch]$BodySceneDepth, [switch]$PlayerGrenades, [switch]$PlayerKnife, [switch]$ContextualTools)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$build=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory,$PSScriptRoot)}else{Join-Path $PSScriptRoot "build/$Architecture"}
. (Join-Path $PSScriptRoot 'tools/StorageBudget.ps1')
$storageAdmission=Assert-FvrStorageBudget -RepositoryRoot $PSScriptRoot -OutputPath $build -Operation Build -PlannedBytes 3GB
$symbols=Join-Path $build 'symbols'
$scratch=Join-Path $build 'scratch'
$previousTemp=$env:TEMP
$previousTmp=$env:TMP
try {
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$env:TEMP=$scratch
$env:TMP=$scratch
$profiles=Join-Path $PSScriptRoot 'profiles/checkpoint202'
$operation=Join-Path $PSScriptRoot 'profiles/checkpoint414'
$receipt=Join-Path $operation 'source-with-header.json'
$proof=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
& (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation | Out-Null
$cycleOption=if($OrdinaryManualCycles){'ON'}else{'OFF'}
if($NativeCycleRecovery -and -not $OrdinaryManualCycles){throw 'Native recovery requires ordinary manual cycles'}
$recoveryOption=if($NativeCycleRecovery){'ON'}else{'OFF'}
if(($OrdinaryBoltInputProbe -or $OrdinaryResourceInputProbe) -and -not $OrdinaryManualCycles){throw 'Ordinary input diagnostic requires ordinary manual cycles'}
if($OrdinaryBoltInputProbe -and $OrdinaryResourceInputProbe){throw 'Select one bounded input driver'}
$inputOption=if($OrdinaryBoltInputProbe){'ON'}else{'OFF'}
$resourceInputOption=if($OrdinaryResourceInputProbe){'ON'}else{'OFF'}
if($OrdinaryResourceInterruptionProbe -and (-not $OrdinaryResourceInputProbe -or -not $NativeCycleRecovery)){
    throw 'Interrupted pump input requires the ordinary resource diagnostic and native cycle recovery'
}
$interruptionOption=if($OrdinaryResourceInterruptionProbe){'ON'}else{'OFF'}
if($OrdinaryResourceSceneProbe -and (-not $OrdinaryResourceInputProbe -or -not $NativeCycleRecovery)){
    throw 'Scene recovery input requires the ordinary resource diagnostic and native cycle recovery'
}
$sceneOption=if($OrdinaryResourceSceneProbe){'ON'}else{'OFF'}
$depthOption=if($BodySceneDepth){'ON'}else{'OFF'}
if(($PlayerGrenades -or $PlayerKnife -or $ContextualTools) -and (-not $OrdinaryManualCycles -or -not $NativeCycleRecovery -or $OrdinaryBoltInputProbe -or $OrdinaryResourceInputProbe -or $OrdinaryResourceInterruptionProbe -or $OrdinaryResourceSceneProbe)){
    throw 'Normal player controls require manual cycles/recovery and no finite input fixture'
}
$playerGrenadesOption=if($PlayerGrenades){'ON'}else{'OFF'}
$playerKnifeOption=if($PlayerKnife){'ON'}else{'OFF'}
$contextualToolsOption=if($ContextualTools){'ON'}else{'OFF'}
& cmake -S $PSScriptRoot -B $build -G Ninja `
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo' '-DBUILD_TESTING=ON' `
    "-DCMAKE_PDB_OUTPUT_DIRECTORY=$symbols" "-DCMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO=$symbols" `
    "-DBC2_ORDINARY_MANUAL_CYCLES=$cycleOption" `
    "-DBC2_NATIVE_CYCLE_RECOVERY=$recoveryOption" `
    "-DBC2_PLAYER_GRENADES=$playerGrenadesOption" `
    "-DBC2_PLAYER_CHEST_KNIFE=$playerKnifeOption" `
    "-DBC2_PLAYER_CONTEXTUAL_TOOLS=$contextualToolsOption" `
    "-DBC2_ORDINARY_BOLT_INPUT_PROBE=$inputOption" `
    "-DBC2_ORDINARY_RESOURCE_INPUT_PROBE=$resourceInputOption" `
    "-DBC2_ORDINARY_RESOURCE_INTERRUPTION_PROBE=$interruptionOption" `
    "-DBC2_ORDINARY_RESOURCE_SCENE_PROBE=$sceneOption" `
    '-DBC2_SCENE_DEPTH_CAPTURE=OFF' `
    '-DBC2_NATIVE_CYCLE_EXPIRED_CONTROL_PROBE=OFF' '-DBC2_NATIVE_FADE_DIAGNOSTIC=OFF' '-DBC2_PUMP_UNREACHABLE_HAND_PROBE=OFF' '-DBC2_PUMP_RECEIVER_GAP_PROBE=OFF' '-DBC2_SHELL_FOCUS_LOSS_PROBE=OFF' '-DBC2_CHEST_KNIFE_PROBE=OFF' '-DBC2_CONTEXTUAL_TOOLS=OFF' '-DBC2_VEHICLE_MOUNTED_PROBE=OFF' '-DBC2_PLAYER_GRENADE_PROBE=OFF' '-DBC2_TWO_GRENADE_DEATH_PROBE=OFF' '-DBC2_ORDINARY_GRENADE_FADE_PROBE=OFF' '-DBC2_ORDINARY_GRENADE_PROBE=OFF' `
    '-DBC2_ORDINARY_RESOURCE_INSPECTION_PROBE=OFF' '-DBC2_ORDINARY_RESOURCE_REVISIT_PROBE=OFF' '-DBC2_ORDINARY_SUPPORT_ONLY_PROBE=OFF' `
    "-DBC2_BODY_SCENE_DEPTH=$depthOption" `
    '-DBC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE=OFF' '-DBC2_ORDINARY_RESOURCE_MAGAZINE_EMPTY_PROBE=OFF' '-DBC2_ORDINARY_RESOURCE_SCENE_MAGAZINE_PROBE=OFF' '-DBC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE=OFF' `
    '-DBC2_ORDINARY_RESOURCE_PAUSE_PROBE=OFF' `
    '-DBC2_ARMING_EMPTY_PROBE=OFF' '-DBC2_AMMO_MOVE_PROBE=OFF' '-DBC2_AMMO_REFILL_PROBE=OFF' '-DBC2_AMMO_RESOURCE_HANDS=OFF' '-DBC2_RESOURCE_MAGAZINES=ON' '-DBC2_MANUAL_BOLT_PROBE=OFF' '-DBC2_MANUAL_PUMP_HANDS=OFF' '-DBC2_MANUAL_PUMP_PROBE=OFF' '-DBC2_M95_STOCK_SHOT_PROBE=OFF' '-DBC2_RESOURCE_INVENTORY_PROBE=OFF' '-DBC2_RESOURCE_PUMP_PROBE=OFF' '-DBC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO=0' `
    '-DBC2_PREHOLD_CLEANUP=ON' '-DBC2_AUTHORED_GRIP_ACCEPTED_BASELINES=OFF' `
    '-DBC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR=' "-DBC2_REVIEWED_MAGAZINE_REGISTRY_HEADER=$PSScriptRoot/profiles/resource-enrollment295-pistol/CombinedRegistry.h" '-DBC2_DRAW_CATALOG_HEADER=' `
    "-DBC2_AUTHORED_GRIP_HEADER=$PSScriptRoot/profiles/aim339/CombinedGrips.h" `
    "-DBC2_AUTHORED_SUPPORT_HEADER=$PSScriptRoot/profiles/support373/CombinedSupport.h" `
    "-DBC2_EXPERIMENTAL_MAGAZINE_HEADER=$PSScriptRoot/profiles/resource-enrollment295-pistol/CombinedGeometry.h" `
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
} finally {
    $env:TEMP=$previousTemp
    $env:TMP=$previousTmp
}


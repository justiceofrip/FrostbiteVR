[CmdletBinding()]
param([string[]]$Suites=@('AmmunitionInventory','Bc2AmmoResourceService','Bc2NativeCycleService','Bc2BodyInventory','Bc2BodyHolster','Bc2MagazineResource','Bc2EquipmentIdentity'))
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$output=Join-Path $repository 'test-temp/chat05'
. (Join-Path $repository 'tools/StorageBudget.ps1')
$allowed=@('AmmunitionInventory','Bc2AmmoResourceService','Bc2NativeCycleService','Bc2BodyInventory','Bc2BodyHolster','Bc2MagazineResource','Bc2EquipmentIdentity')
foreach($suite in $Suites){if($suite -notin $allowed){throw "Unknown suite: $suite"}}
# Include the canonical retained output roots; a worktree is not a fresh budget.
$canonicalRoots=Get-FvrStorageRoots -RepositoryRoot 'G:/Unity/VRMMO/FrostbiteVR'
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output -Operation Build -PlannedBytes 64MB -AdditionalRoots $canonicalRoots
$null=[IO.Directory]::CreateDirectory($output)
$logs=Join-Path $output ('attempt-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$null=[IO.Directory]::CreateDirectory($logs)
function SaveText([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
SaveText (Join-Path $logs 'storage-admission.json') ($admission | ConvertTo-Json -Depth 10)
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
& (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$sources=@(
 'src/math/StereoMath.cpp','src/math/WeaponPoseMath.cpp','src/engines/frostbite/BindingValidation.cpp',
 'src/engines/frostbite/PeImage.cpp','src/games/bc2/Bc2Profile.cpp',
 'src/interaction/ActionPolicy.cpp','src/interaction/ComfortControls.cpp',
 'src/interaction/ControllerInput.cpp','src/interaction/TrackingMath.cpp','src/interaction/RigPose.cpp',
 'src/interaction/TrackedRig.cpp','src/interaction/ArmIk.cpp','src/interaction/WeaponGrip.cpp',
 'src/interaction/SupportGrip.cpp','src/interaction/AmmoSupply.cpp','src/interaction/DetachableMagazine.cpp',
 'src/games/bc2/Bc2BodyInventory.cpp','src/games/bc2/Bc2BodyHolster.cpp','src/games/bc2/Bc2HolsterInput.cpp',
 'src/games/bc2/Bc2WeaponVisibility.cpp','src/games/bc2/Bc2SelectedMeshes1p.cpp','src/games/bc2/Bc2InputBinding.cpp',
 'src/games/bc2/Bc2NativeCycleService.cpp','src/games/bc2/Bc2NativeCycleRecovery.cpp','src/games/bc2/Bc2NativeCycleEvidence.cpp',
 'src/games/bc2/Bc2ReloadFlow.cpp','src/games/bc2/Bc2ReloadState.cpp','src/games/bc2/Bc2ReloadHold.cpp',
 'src/games/bc2/Bc2PhysicalPump.cpp','src/games/bc2/Bc2PumpPart.cpp',
 'src/games/bc2/Bc2Rig.cpp','src/games/bc2/Bc2ReloadPresentation.cpp',
 'src/games/bc2/Bc2BeltAmmo.cpp','src/games/bc2/Bc2WeaponMode.cpp','src/games/bc2/Bc2MagazineDetachGate.cpp',
 'src/games/bc2/Bc2AmmoSupply.cpp','src/games/bc2/Bc2ReloadInteraction.cpp','src/games/bc2/Bc2MagazineDetachEvidence.cpp',
 'src/games/bc2/Bc2ReloadRequestBridge.cpp',
 'src/games/bc2/Bc2MagazineResourceReload.cpp','src/games/bc2/Bc2MagazinePhysicalReload.cpp',
 'src/games/bc2/Bc2MagazineInteraction.cpp','src/games/bc2/Bc2MagazinePresentation.cpp',
 'src/games/bc2/Bc2MagazineGeometryProfile.cpp','src/games/bc2/Bc2MagazineNativeRegistry.cpp'
)
$common=@('/nologo','/std:c++20','/EHsc','/O1','/Gy','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX')
foreach($include in @('include','include/fvr/interaction','include/fvr/math','src/games/bc2','tests')){$common+='/I'+(Join-Path $repository $include)}
Push-Location $output
try {
 # Standalone CPU objects only: no CMake, normal build tree, DLL or game process.
 $absoluteSources=@($sources | ForEach-Object {Join-Path $repository $_})
 $compile=& cl @common /MP4 /c @absoluteSources 2>&1
 SaveText (Join-Path $logs 'compile.log') ($compile -join "`n")
 if($LASTEXITCODE -ne 0){$compile | Select-Object -Last 40;throw 'Isolated source compilation failed.'}
 $objects=@($sources | ForEach-Object {Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($_))+'.obj')})
 $library=Join-Path $output 'Chat05Cpu.lib'
 $archive=& lib /nologo "/OUT:$library" @objects 2>&1
 SaveText (Join-Path $logs 'archive.log') ($archive -join "`n")
 if($LASTEXITCODE -ne 0){throw 'Isolated CPU archive failed.'}
 foreach($suite in $Suites){
  $test=Join-Path $repository "tests/${suite}Tests.cpp"
  $exe=Join-Path $output "${suite}Tests.exe"
  $linked=& cl @common $test $library "/Fe:$exe" /link /OPT:REF /INCREMENTAL:NO 2>&1
  SaveText (Join-Path $logs "$suite-build.log") ($linked -join "`n")
  if($LASTEXITCODE -ne 0){$linked | Select-Object -Last 40;throw "$suite compilation/link failed."}
  $result=& $exe 2>&1
  $exitCode=$LASTEXITCODE
  SaveText (Join-Path $logs "$suite-result.txt") (($result -join "`n")+"`nExit code: $exitCode`n")
  $result
  if($exitCode -ne 0){throw "$suite failed ($exitCode)."}
 }
} finally {Pop-Location}

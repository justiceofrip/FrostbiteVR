[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x86',[Parameter(Mandatory)][string]$OutputPath,[string]$ReuseSyntaxReport,[string[]]$OnlySuites)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=[IO.Path]::GetFullPath($OutputPath)
if(-not $output.StartsWith((Join-Path $work 'reports')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Report root required'}
if(Test-Path -LiteralPath $output){throw 'Fresh report directory required'}
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 512MB -AdditionalRoots @((Join-Path $canonical 'build'),'G:/Unity/VRMMO/bc2vr-recovery')
$null=[IO.Directory]::CreateDirectory($output)
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $output $name),($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
Save 'storage.json' $budget
$env:TEMP=Join-Path $output 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$null=[IO.Directory]::CreateDirectory($env:TEMP)
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$common=@('/nologo','/std:c++20','/EHsc','/MT','/O1','/UNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'tests')))
$defs=@{
 'FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER'='profiles/weapons45-magazines/CombinedRegistry.h'
 'FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER'='profiles/weapons45-magazines/CombinedGeometry.h'
 'FVR_BC2_FEED_REGISTRY_HEADER'='profiles/mechanism-family41/Feed.generated.h'
 'FVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER'='profiles/mechanism-family41/Slide.generated.h'
 'BC2_SHOTGUN_EQUIPMENT_HEADER'='profiles/weapons43/ShotgunEquipment.generated.h'
 'BC2_BOLT_EQUIPMENT_HEADER'='profiles/weapons43/BoltEquipment.generated.h'
}
foreach($entry in $defs.GetEnumerator()){$common+='/D'+$entry.Key+'="'+((Join-Path $work $entry.Value)-replace '\\','/')+'"'}
$common+=@('/DFVR_BC2_NATIVE_CYCLE_RECOVERY','/DFVR_BC2_ORDINARY_BOLT','/DFVR_BC2_ORDINARY_MANUAL_CYCLES','/DFVR_BC2_MANUAL_CYCLE_CANDIDATE')
$names=@('Bc2Rig','Bc2AmmoSupply','Bc2ReloadInteraction','Bc2PhysicalReload','Bc2BeltAmmo','Bc2ReloadPresentation','Bc2ReloadPreview','Bc2ReloadPalette','Bc2ReloadRequestBridge','Bc2ReloadHold','Bc2ReloadRoundGate','Bc2ReloadRequestCycle','Bc2PhysicalBolt','Bc2BoltPart','Bc2BoltPlayerCustody','Bc2BoltTrackedMapping','Bc2PhysicalPump','Bc2PumpPart','Bc2NativeCycleEvidence','Bc2NativeCycleService','Bc2NativeCycleRecovery','Bc2PhysicalFixedContainer','Bc2FixedContainerPresentation','Bc2MagazineNativeRegistry','Bc2MagazineGeometryProfile','Bc2FeedRegistry','Bc2PistolSlideRegistry','Bc2BodyAmmoRenderer','Bc2BodyAmmoGeometryCache')
$sources=@($names|ForEach-Object {Join-Path $work ('src/games/bc2/'+$_+'.cpp')})
$sources+=Join-Path $work 'src/interaction/RigPose.cpp'
foreach($source in $sources){if(-not(Test-Path -LiteralPath $source)){throw "Missing source $source"}}
$suites=@('BoltReloadDebt','Bc2NativeCycleReload','FixedContainerReload','Bc2FixedContainerResource','Bc2CycleReloadRouting','Bc2PhysicalFixedContainer','FixedContainerContext','Bc2BoltReloadRuntime','Bc2ShotgunEquipment','Bc2BoltEquipment','Bc2Rig','Bc2PhysicalBolt','Bc2PhysicalBoltRecovery','Bc2BoltPlayerCustody','Bc2PhysicalPump','Bc2PhysicalPumpRecovery','Bc2NativeCycleService','Bc2NativeCycleEvidence','Bc2NativeCycleRecoveryService','AmmunitionLedger','AmmunitionInventory','Bc2AmmoResourceService','Bc2AmmoResourceEvidence','Bc2ReloadInteraction','Bc2ReloadPresentation','Bc2ReloadPalette','Bc2BodyAmmo','Bc2BodyPropPair','FeedInteraction','Bc2BodyAmmoCache')
$libraries=@('BC2Camera.lib','BC2Profile.lib','FrostbiteDiscovery.lib','FrostbiteCamera.lib','FvrCore.lib','FvrD3D11RigidProp.lib','FvrRigidPropGeometry.lib')|ForEach-Object {Join-Path $canonical ('build/'+$Architecture+'/'+$_)}
$libraries+=@('d3d11.lib','d3dcompiler.lib')
if($OnlySuites){
 foreach($suite in $OnlySuites){if($suite -notin $suites){throw "Unknown suite $suite"}}
 $suites=$OnlySuites
}
Push-Location $output
try {
 $ninja=Get-Content -LiteralPath (Join-Path $canonical ('build/'+$Architecture+'/build.ninja'))
 $syntax=@();$notBuilt=@()
 $syntaxNames=@('Bc2ReloadFlowRuntime.cpp','Bc2Gameplay.cpp','Bc2RigPublication.cpp','NativeProbe.cpp')
 if($ReuseSyntaxReport){
  $syntax=@(Get-Content -LiteralPath (Join-Path $ReuseSyntaxReport 'syntax-results.json') -Raw|ConvertFrom-Json)
  if($syntax.Count -ne 4 -or $syntax.Where({-not $_.passed}).Count){throw 'Cannot reuse failed syntax receipt'}
  Save 'reused-syntax.json' @{source=$ReuseSyntaxReport;reason='Only implementation includes changed after production syntax pass; production inputs unchanged'}
  $syntaxNames=@()
 }
 foreach($name in $syntaxNames){
  $at=-1
  for($n=0;$n -lt $ninja.Count;$n++){if($ninja[$n].StartsWith('build ') -and $ninja[$n].Contains($name+'.obj:')){$at=$n;break}}
  if($at -lt 0){
   if($Architecture -eq 'x64' -and $name -in @('Bc2Gameplay.cpp','Bc2RigPublication.cpp','NativeProbe.cpp')){
    $notBuilt+=$name;Write-Output "$name is x86-only in the existing build configuration";continue
   }
   throw "Missing existing compile configuration $name"
  }
  $defineLine=$ninja[($at+1)..($at+6)]|Where-Object {$_.StartsWith('  DEFINES = ')}|Select-Object -First 1
  $nativeDefs=@($defineLine.Substring(12).Split(' ',[StringSplitOptions]::RemoveEmptyEntries)|ForEach-Object {
   $_.Replace('\"','"').Replace(($canonical-replace '\\','/'),($work-replace '\\','/')).Replace($canonical,$work)
  }|Where-Object {$_ -notmatch 'FVR_BC2_AUTHORED_SUPPORT_HEADER=' -and $_ -notmatch 'FVR_BC2_(EXPERIMENTAL_MAGAZINE|REVIEWED_MAGAZINE_REGISTRY|FEED_REGISTRY|PISTOL_SLIDE_REGISTRY)_HEADER='})
  $nativeArgs=$common+$nativeDefs+@('/Zs',('/I'+(Join-Path $canonical ('build/'+$Architecture+'/generated/bc2-operation'))),('/I'+(Join-Path $work 'third_party/minhook-1.3.4/include')),('/DFVR_BC2_AUTHORED_SUPPORT_HEADER="'+((Join-Path $work 'profiles/weapons43/CombinedSupport.h')-replace '\\','/')+'"'),(Join-Path $work ('src/games/bc2/'+$name)))
  Save ($name+'-syntax-command.json') @{arguments=$nativeArgs}
  & cl.exe @nativeArgs *> ($name+'-syntax.log')
  $okay=$LASTEXITCODE -eq 0;$syntax+=@{file=$name;passed=$okay}
  Write-Output "$name syntax=$okay"
  if(-not $okay){Get-Content ($name+'-syntax.log') -Tail 45}
 }
 Save 'syntax-results.json' $syntax
 Save 'syntax-not-built.json' $notBuilt
 if($syntax.Where({-not $_.passed}).Count){throw 'Production source syntax failed'}
 Save 'source-inputs.json' @($sources|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}})
 $arguments=$common+@('/c','/MP4')+$sources
 Save 'component-command.json' @{compiler=(Get-Command cl.exe).Source;arguments=$arguments}
 & cl.exe @arguments *> 'components.log'
 if($LASTEXITCODE -ne 0){Get-Content 'components.log' -Tail 60;throw 'Changed component compilation failed'}
 $objects=@(Get-ChildItem -LiteralPath $output -Filter '*.obj'|ForEach-Object FullName)
 & lib.exe /nologo /OUT:WeaponsCandidate.lib @objects *> 'archive.log'
 if($LASTEXITCODE -ne 0){throw 'Component archive failed'}
 $results=@()
 foreach($suite in $suites){
  $test=Join-Path $work ('tests/'+$suite+'Tests.cpp')
  if(-not(Test-Path -LiteralPath $test)){throw "Missing test $test"}
  $args=$common+@($test,('/Fe:'+$suite+'.exe'),'/link','WeaponsCandidate.lib')+$libraries
  & cl.exe @args *> ($suite+'-compile.log')
  $compiled=$LASTEXITCODE -eq 0;$passed=$false
  if($compiled){
   $testArgs=@()
   if($suite -eq 'Bc2BodyAmmoCache'){$testArgs=@((Join-Path $work 'reports/coordinator-20261030-cache48-enrollment01/body-ammo.fvrprop'),'252')}
   & (Join-Path $output ($suite+'.exe')) @testArgs *> ($suite+'-test.log');$passed=$LASTEXITCODE -eq 0
  }
  $results+=@{suite=$suite;compiled=$compiled;passed=$passed}
  Write-Output "$suite compiled=$compiled passed=$passed"
  if(-not $compiled){Get-Content ($suite+'-compile.log') -Tail 16}
  elseif(-not $passed){Get-Content ($suite+'-test.log') -Tail 16}
  Save 'results.json' @{architecture=$Architecture;checks=$results;normal_build=$false;installed=$false}
 }
 if($results.Where({-not $_.passed}).Count){throw 'Focused checks failed; see results.json'}
} finally {Pop-Location}

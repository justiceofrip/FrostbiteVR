[CmdletBinding()]
param(
 [ValidateSet('x86','x64')][string]$Architecture='x86',
 [Parameter(Mandatory)][string]$OutputPath,
 [string[]]$OnlySuites=@(),
 [string]$ComponentReport,
 [string[]]$Recompile=@()
)
# Staged deterministic/component checks only. Canonical build trees are read-only inputs.
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=[IO.Path]::GetFullPath($OutputPath)
$reportRoot=(Join-Path $work 'reports')+[IO.Path]::DirectorySeparatorChar
if(-not $output.StartsWith($reportRoot,[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetPathRoot($output) -ne 'G:\' -or (Test-Path -LiteralPath $output)){
 throw 'A fresh G: report directory inside this worktree is required'
}
if($Recompile -and -not $ComponentReport){throw 'Recompile requires a ComponentReport'}

function Save($name,$value){
 [IO.File]::WriteAllText((Join-Path $output $name),($value|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
}
function InputReceipt($paths){
 @($paths|Sort-Object -Unique|ForEach-Object {
  if(-not(Test-Path -LiteralPath $_ -PathType Leaf)){throw "Missing input $_"}
  @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
 })
}
function CheckReceipt($receipts){
 foreach($entry in $receipts){
  if(-not(Test-Path -LiteralPath $entry.path -PathType Leaf) -or
     (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256){
   throw "Reused input changed: $($entry.path). Start a fresh Run-Checks report."
  }
 }
}
function HeaderReceipt {
 $paths=@(foreach($folder in @('include','src','profiles')){
  Get-ChildItem -LiteralPath (Join-Path $work $folder) -Recurse -File |
   Where-Object {$_.Extension -in @('.h','.hpp','.inl')} | ForEach-Object FullName
 })
 InputReceipt $paths
}
function NinjaField($lines,[int]$start,[string]$field){
 for($line=$start+1;$line -lt $lines.Count -and $lines[$line].StartsWith('  ');$line++){
  if($lines[$line].StartsWith("  $field = ")){return $lines[$line].Substring($field.Length+5)}
 }
 return ''
}
function NinjaArguments([string]$line){
 @([regex]::Matches($line,'(?:[^\s"]|"[^"]*")+')|ForEach-Object {
  $_.Value.Replace('\"','"').Replace(($canonical-replace '\\','/'),($work-replace '\\','/')).Replace($canonical,$work)
 })
}

$common=@('/nologo','/std:c++20','/EHsc','/MT','/O1','/UNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
 ('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'tests')))
$defs=[ordered]@{
 FVR_BC2_AUTHORED_SUPPORT_HEADER='profiles/weapons43/CombinedSupport.h'
 FVR_BC2_AUTHORED_AIM_HEADER='profiles/weapons56-aim/Aim.generated.h'
 FVR_BC2_MUZZLE_ENROLLMENT_HEADER='profiles/weapons54-muzzle/CombinedMuzzle.h'
 FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER='profiles/weapons45-magazines/CombinedRegistry.h'
 FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER='profiles/weapons45-magazines/CombinedGeometry.h'
 FVR_BC2_FEED_REGISTRY_HEADER='profiles/mechanism-family41/Feed.generated.h'
 FVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER='profiles/mechanism-family41/Slide.generated.h'
 BC2_SHOTGUN_EQUIPMENT_HEADER='profiles/weapons43/ShotgunEquipment.generated.h'
 BC2_BOLT_EQUIPMENT_HEADER='profiles/weapons43/BoltEquipment.generated.h'
}
foreach($entry in $defs.GetEnumerator()){
 $header=Join-Path $work $entry.Value
 if(-not(Test-Path -LiteralPath $header -PathType Leaf)){throw "Missing enrollment header $header"}
 $common+='/D'+$entry.Key+'="'+($header-replace '\\','/')+'"'
}
$common+=@('/DFVR_BC2_NATIVE_CYCLE_RECOVERY','/DFVR_BC2_ORDINARY_BOLT','/DFVR_BC2_ORDINARY_MANUAL_CYCLES','/DFVR_BC2_MANUAL_CYCLE_CANDIDATE')
$names=@('Bc2Rig','Bc2AmmoSupply','Bc2ReloadInteraction','Bc2PhysicalReload','Bc2BeltAmmo',
 'Bc2ReloadPresentation','Bc2ReloadPreview','Bc2ReloadPalette','Bc2ReloadRequestBridge','Bc2ReloadHold',
 'Bc2ReloadRoundGate','Bc2ReloadRequestCycle','Bc2PhysicalBolt','Bc2BoltPart','Bc2BoltPlayerCustody',
 'Bc2BoltTrackedMapping','Bc2PhysicalPump','Bc2PumpPart','Bc2NativeCycleEvidence','Bc2NativeCycleService',
 'Bc2NativeCycleRecovery','Bc2PhysicalFixedContainer','Bc2FixedContainerPresentation','Bc2MagazineNativeRegistry',
 'Bc2MagazineGeometryProfile','Bc2FeedRegistry','Bc2PistolSlideRegistry','Bc2BodyAmmoRenderer','Bc2BodyAmmoGeometryCache',
 'Bc2MagazinePhysicalReload','Bc2MagazineResourceReload','Bc2MagazineInteraction','Bc2MagazinePresentation',
 'Bc2MagazineDetached','Bc2MagazineDetachedProbe',
 'Bc2WeaponMode','Bc2PhysicalFeed','Bc2FeedAmmoSupply')
$sources=@($names|ForEach-Object {Join-Path $work ('src/games/bc2/'+$_+'.cpp')})
$sources+=Join-Path $work 'src/interaction/RigPose.cpp'
$sources+=Join-Path $work 'src/graphics/d3d11/D3D11SceneDepthPair.cpp'
$sources+=Join-Path $work 'src/graphics/D3D11BodyPropCompositor.cpp'
$sources+=Join-Path $work 'src/graphics/BodyPropMaterials.cpp'
$sourceInputs=InputReceipt $sources
$sourceByName=@{}
foreach($source in $sources){$sourceByName[[IO.Path]::GetFileNameWithoutExtension($source)]=$source}
$suiteNames=@('Bc2PhysicalFeed','Bc2FeedPresentation','FeedBodyAmmoIntegration',
 'Bc2MagazineAssembly','WeaponMagazineEntry','Bc2MagazinePhysical','Bc2WeaponFrameAccess',
 'Bc2AuthoredAim','Bc2AuthoredGrip','Bc2AuthoredSupport','WeaponFamilyMuzzle','Bc2FireRouteTelemetry','HostFrameDiagnostics')
$gameLibraries=@('BC2Camera.lib','BC2Profile.lib','FrostbiteDiscovery.lib','FrostbiteCamera.lib','FvrCore.lib')
$suiteSpecs=@(foreach($suite in $suiteNames){
 $libs=$gameLibraries;$system=@();$testArgs=@();$extraSources=@();$extraCompiler=@()
 if($suite -eq 'WeaponFamilyAmmoPublication'){
  $extraSources=@((Join-Path $work 'src/games/bc2/Bc2ReloadFlowRuntime.cpp'))
  $extraCompiler=@(('/I'+(Join-Path $work 'third_party/minhook-1.3.4/include')))
  if($Architecture -eq 'x86'){$libs+=@('FvrMinHook.lib')}
 }
 if($suite -in @('Bc2BodyAmmoCache','FeedBodyAmmoIntegration')){
  $libs+=@('FvrD3D11RigidProp.lib','FvrRigidPropGeometry.lib');$system=@('d3d11.lib','d3dcompiler.lib')
  if($suite -eq 'Bc2BodyAmmoCache'){
   $testArgs=@((Join-Path $work 'reports/coordinator-20261030-wiring54/feed-cache01/body-ammo.fvrprop'),'264')
  }
 } elseif($suite -eq 'SceneDepthPairGpu'){
  $libs=@('FvrD3D11FrameBridge.lib','FvrD3D11RigidProp.lib','FvrD3D11.lib','FvrFrameChannel.lib','FvrRigidPropGeometry.lib','FvrCore.lib')
  $system=@('d3d11.lib','d3dcompiler.lib','dxgi.lib','ole32.lib','user32.lib');$testArgs=@('--legacy-depth-cache')
 } elseif($suite -eq 'SceneDepthFrame'){$libs=@('FvrFrameChannel.lib','FvrCore.lib');$system=@('ole32.lib')}
 elseif($suite -eq 'HostFrameDiagnostics'){$libs=@()}
 $test=Join-Path $work ('tests/'+$suite+'Tests.cpp')
 if(-not(Test-Path -LiteralPath $test -PathType Leaf)){throw "Missing test $test"}
 @{name=$suite;source=$test;extra_sources=$extraSources;extra_compiler=$extraCompiler;libraries=@($libs|ForEach-Object {Join-Path $canonical ('build/'+$Architecture+'/'+$_)})+$system;arguments=$testArgs}
})
$libraries=@($suiteSpecs.libraries|Where-Object {[IO.Path]::IsPathRooted($_)}|Sort-Object -Unique)
$libraryInputs=InputReceipt $libraries
$headers=HeaderReceipt
$selected=$suiteNames
$overrides=@{}
$archive=Join-Path $output 'WeaponsCandidate.lib'
$prior=$null
if($ComponentReport){
 $ComponentReport=[IO.Path]::GetFullPath($ComponentReport)
 if(-not $ComponentReport.StartsWith($reportRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'ComponentReport must be a report in this worktree'}
 $prior=Get-Content -LiteralPath (Join-Path $ComponentReport 'component-manifest.json') -Raw|ConvertFrom-Json
 if($prior.architecture -ne $Architecture -or $prior.worktree -ne $work){throw 'ComponentReport architecture/worktree mismatch'}
 CheckReceipt $prior.library_inputs
 $oldHeaders=@{};foreach($entry in $prior.header_inputs){$oldHeaders[$entry.path]=$entry.sha256}
 if($headers.Count -ne $oldHeaders.Count -or @($headers|Where-Object {$oldHeaders[$_.path] -ne $_.sha256}).Count){
  throw 'Header/profile inputs changed; use a fresh full Run-Checks report'
 }
 if((@($prior.common)-join "`n") -ne ($common-join "`n")){throw 'Harness compilation settings changed; use a fresh full Run-Checks report'}
 foreach($name in $Recompile){if(-not $sourceByName.ContainsKey($name)){throw "Unknown component $name"}}
 foreach($entry in $prior.source_inputs){
  if([IO.Path]::GetFileNameWithoutExtension($entry.path) -notin $Recompile){CheckReceipt @($entry)}
 }
 if($prior.source_inputs.Count -ne $sourceInputs.Count){throw 'Component set changed; use a fresh full Run-Checks report'}
 CheckReceipt @($prior.archive_input)
 $archive=$prior.archive_input.path
 foreach($entry in $prior.object_inputs){CheckReceipt @($entry);$overrides[[IO.Path]::GetFileNameWithoutExtension($entry.path)]=$entry.path}
 $previous=Get-Content -LiteralPath (Join-Path $ComponentReport 'results.json') -Raw|ConvertFrom-Json
 $selected=@($previous.checks|Where-Object {-not $_.passed}|ForEach-Object suite)
}
if($OnlySuites){$selected=@($OnlySuites|Select-Object -Unique)}
foreach($suite in $selected){if($suite -notin $suiteNames){throw "Unknown suite $suite"}}
if(-not $selected.Count){throw 'No failed suites selected; specify affected suites when recompiling a source'}
$selectedSpecs=@($suiteSpecs|Where-Object {$_.name -in $selected})
foreach($spec in $selectedSpecs){
 if($spec.name -eq 'Bc2BodyAmmoCache' -and -not(Test-Path -LiteralPath $spec.arguments[0] -PathType Leaf)){
  throw "Stage the 264-part cache first: $($spec.arguments[0])"
 }
}

$savedTemp=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR}
$pushed=$false
try {
 . (Join-Path $work 'tools/StorageBudget.ps1')
 $planned=if($ComponentReport){256MB}else{512MB}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes $planned -AdditionalRoots @((Join-Path $canonical 'build'),'G:/Unity/VRMMO/bc2vr-recovery')
 $null=[IO.Directory]::CreateDirectory($output)
 Save 'storage.json' $budget
 $env:TEMP=Join-Path $output 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
 $null=[IO.Directory]::CreateDirectory($env:TEMP)
 $vs='C:\Program Files\Microsoft Visual Studio\18\Community'
 $arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
 & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
 Push-Location $output;$pushed=$true
 $syntax=@();$notBuilt=@()
 if(-not $ComponentReport){
  $ninjaPath=Join-Path $canonical ('build/'+$Architecture+'/build.ninja')
  $ninja=Get-Content -LiteralPath $ninjaPath
  $syntaxSources=@('src/games/bc2/Bc2ReloadFlowRuntime.cpp','src/games/bc2/Bc2Gameplay.cpp',
   'src/games/bc2/Bc2RigPublication.cpp','src/games/bc2/NativeProbe.cpp','src/xr/OpenXrHost.cpp')
  foreach($relative in $syntaxSources){
   $name=[IO.Path]::GetFileName($relative);$at=-1
   for($n=0;$n -lt $ninja.Count;$n++){
    if($ninja[$n].StartsWith('build ') -and $ninja[$n].Contains($name+'.obj:')){$at=$n;break}
   }
   if($at -lt 0){
    if(($Architecture -eq 'x86' -and $name -eq 'OpenXrHost.cpp') -or
       ($Architecture -eq 'x64' -and $name -in @('Bc2Gameplay.cpp','Bc2RigPublication.cpp','NativeProbe.cpp'))){
     $notBuilt+=@{file=$relative;architecture=$Architecture;reason='Not compiled in this canonical architecture'};continue
    }
    throw "Missing existing compile configuration $name"
   }
   $nativeDefs=@(NinjaArguments (NinjaField $ninja $at 'DEFINES')|Where-Object {
    $key=($_ -replace '^[-/]D','') -replace '=.*$',''
    -not $defs.Contains($key)
   })
   # Preserve canonical generated-operation paths; all other includes use this candidate's sources.
   $nativeIncludes=@(NinjaArguments (NinjaField $ninja $at 'INCLUDES')|ForEach-Object {
    $_.Replace((Join-Path $work 'build'),(Join-Path $canonical 'build')).Replace(($work-replace '\\','/')+'/build',($canonical-replace '\\','/')+'/build')
   })
   $nativeArgs=$common+$nativeDefs+$nativeIncludes+@('/Zs',(Join-Path $work $relative))
   Save ($name+'-syntax-command.json') @{architecture=$Architecture;ninja=$ninjaPath;arguments=$nativeArgs}
   & cl.exe @nativeArgs *> ($name+'-syntax.log')
   $okay=$LASTEXITCODE -eq 0;$syntax+=@{file=$relative;passed=$okay}
   Save 'syntax-results.json' $syntax
   Write-Output "$name syntax=$okay"
   if(-not $okay){Get-Content ($name+'-syntax.log') -Tail 45}
  }
  Save 'syntax-not-built.json' $notBuilt
  if(@($syntax|Where-Object {-not $_.passed}).Count){throw 'Production source syntax failed'}
  $arguments=$common+@('/c','/MP4')+$sources
  Save 'component-command.json' @{compiler=(Get-Command cl.exe).Source;arguments=$arguments}
  & cl.exe @arguments *> 'components.log'
  if($LASTEXITCODE -ne 0){Get-Content 'components.log' -Tail 60;throw 'Changed component compilation failed'}
  $objects=@($sources|ForEach-Object {Join-Path $output ([IO.Path]::GetFileNameWithoutExtension($_)+'.obj')})
  & lib.exe /nologo ('/OUT:'+$archive) @objects *> 'archive.log'
  if($LASTEXITCODE -ne 0){Get-Content 'archive.log' -Tail 30;throw 'Component archive failed'}
 } else {
  Save 'syntax-skipped.json' @{reason='Targeted suite/source rerun only; no new production syntax claim';component_report=$ComponentReport}
  foreach($name in $Recompile|Select-Object -Unique){
   $arguments=$common+@('/c',$sourceByName[$name],('/Fo:'+ (Join-Path $output ($name+'.obj'))))
   Save ($name+'-component-command.json') @{arguments=$arguments}
   & cl.exe @arguments *> ($name+'-component-compile.log')
   if($LASTEXITCODE -ne 0){Get-Content ($name+'-component-compile.log') -Tail 30;throw "Component recompile failed: $name"}
   $overrides[$name]=Join-Path $output ($name+'.obj')
  }
 }
 Save 'component-manifest.json' @{architecture=$Architecture;worktree=$work;common=$common;
  source_inputs=$sourceInputs;header_inputs=$headers;library_inputs=$libraryInputs;
  archive_input=@(InputReceipt @($archive))[0];object_inputs=@(InputReceipt @($overrides.Values));component_report=$ComponentReport}
 $results=@()
 foreach($spec in $selectedSpecs){
  $suite=$spec.name
  $arguments=$common+$spec.extra_compiler+$spec.extra_sources+@($spec.source,('/Fe:'+$suite+'.exe'),'/link','/INCREMENTAL:NO','/DEBUG:NONE')+
   @($overrides.Values)+@($archive)+$spec.libraries
  Save ($suite+'-command.json') @{arguments=$arguments;test_arguments=$spec.arguments;test_source=@(InputReceipt @($spec.source))[0];extra_sources=@(InputReceipt @($spec.extra_sources))}
  & cl.exe @arguments *> ($suite+'-compile.log')
  $compiled=$LASTEXITCODE -eq 0;$passed=$false
  if($compiled){
   $testArgs=@($spec.arguments)
   & (Join-Path $output ($suite+'.exe')) @testArgs *> ($suite+'-test.log')
   $passed=$LASTEXITCODE -eq 0
  }
  $results+=@{suite=$suite;compiled=$compiled;passed=$passed;arguments=$spec.arguments}
  Write-Output "$suite compiled=$compiled passed=$passed"
  if(-not $compiled){Get-Content ($suite+'-compile.log') -Tail 24}
  elseif(-not $passed){Get-Content ($suite+'-test.log') -Tail 24}
  Save 'results.json' @{architecture=$Architecture;checks=$results;component_report=$ComponentReport;
   production_syntax_checked=(-not [bool]$ComponentReport);normal_build=$false;installed=$false;game_started=$false;headset_checked=$false}
 }
 if(@($results|Where-Object {-not $_.passed}).Count){throw 'Focused checks failed; see results.json'}
} finally {
 if($pushed){Pop-Location}
 $env:TEMP=$savedTemp.TEMP;$env:TMP=$savedTemp.TMP;$env:TMPDIR=$savedTemp.TMPDIR
}

[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture,
      [ValidateSet('Anchor','Input','GameplaySyntax','NativeSyntax','ReloadSyntax','RigSyntax','Sight','Scope','Grip','Configuration','Carried','Observation','Muzzle','FeedMechanism','FeedReload','FeedInteraction','FeedReloadSession','FeedAmmunition','FeedEquipmentSession','PhysicalPistolSlide','Bc2FeedEquipmentAdapter','Bc2FeedEquipmentProfile','Bc2PhysicalFeed','Bc2FeedPresentation','PistolAfterFeedPolicy','Bc2PistolSlideProfile','Bc2PistolSlidePresentation','Bc2PistolAfterFeedEvidence','Bc2PistolAfterFeedInput','Bc2PhysicalPistolSlide')][string]$Suite,
      [ValidateSet('coordinator-20261029-polish11','coordinator-20261029-work12','coordinator-20261029-work13','coordinator-20261030-implementation15','coordinator-20261030-implementation17','coordinator-20261030-implementation18','coordinator-20261030-implementation22')][string]$ReportRun='coordinator-20261029-polish11',
      [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=[IO.Path]::GetFullPath($OutputPath)
$reportRoot=Join-Path $work ('reports/'+$ReportRun)
if(-not $output.StartsWith($reportRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside this run' }
if(Test-Path -LiteralPath $output) { throw 'Use a fresh output folder; failed checks must remain' }
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 96MB -RawCaptureBytes 0 -AdditionalRoots @((Join-Path $canonical 'build'),(Join-Path $canonical 'reports'),'G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$null=New-Item -ItemType Directory -Path $output
function SaveJson($Name,$Value) { [IO.File]::WriteAllText((Join-Path $output $Name),($Value|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false)) }
SaveJson 'storage.json' $budget
$env:TEMP=Join-Path $reportRoot 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$null=New-Item -ItemType Directory -Path $env:TEMP -Force
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
if($env:VSCMD_ARG_TGT_ARCH -ne $Architecture -or -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$compiler=(Get-Command cl.exe).Source
if((Split-Path -Leaf (Split-Path -Parent $compiler)) -ne $Architecture) {
    throw 'Compiler architecture does not match requested check; use a fresh shell for each architecture'
}
$inputs=[Collections.Generic.List[string]]::new()
$arguments=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/UNDEBUG','/DNOMINMAX',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'tests')))
if($Suite -in @('GameplaySyntax','NativeSyntax','ReloadSyntax','RigSyntax')) {
    if($Architecture -ne 'x86'){throw 'Native gameplay is an x86 target'}
    $source=switch($Suite){'NativeSyntax'{'NativeProbe.cpp'}'ReloadSyntax'{'Bc2ReloadFlowRuntime.cpp'}'RigSyntax'{'Bc2RigPublication.cpp'}default{'Bc2Gameplay.cpp'}}
    $ninja=Get-Content -LiteralPath (Join-Path $canonical 'build/x86/build.ninja')
    $index=0;while($index -lt $ninja.Count -and $ninja[$index] -notlike ('build *'+$source+'.obj:*')) {++$index}
    if($index -eq $ninja.Count){throw 'No baseline gameplay compile rule'}
    $definitionLine=($ninja | Select-Object -Skip ($index+1) -First 6 | Where-Object {$_ -like '  DEFINES = *'})
    $defines=($definitionLine.Substring(12) -split ' ')|ForEach-Object {$_.Replace('\"','"')}
    $inputs.Add((Join-Path $work ('src/games/bc2/'+$source)))
    $arguments+=@('/Zs','/DWIN32','/D_WINDOWS','/DWIN32_LEAN_AND_MEAN',('/I'+(Join-Path $canonical 'build/x86/generated/bc2-operation')),('/I'+(Join-Path $work 'third_party/minhook-1.3.4/include'))) + $defines + $inputs.ToArray()
} else {
    $test=switch($Suite){'Anchor'{'TrackedBodyAnchorTests'}'Input'{'Bc2InputBindingTests'}'Sight'{'Bc2SightAdapterTests'}'Scope'{'Bc2SelectedMeshes1pTests'}'Grip'{'TrackedRigTests'}'Configuration'{'Bc2SelectedConfigurationPathTests'}'Carried'{'Bc2CarriedMeshesTests'}'Observation'{'Bc2SelectedMeshesObservationTests'}'Muzzle'{'Bc2MuzzleAdmissionTests'}default{$Suite+'Tests'}}
    $inputs.Add((Join-Path $work ('tests/'+$test+'.cpp')))
    if($Suite -eq 'Input'){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2InputBinding.cpp'))}
    if($Suite -in @('Scope','Configuration','Carried','Observation')){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2SelectedMeshes1p.cpp'))}
    if($Suite -in @('Carried','Observation')){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2SelectedMeshesObservation.cpp'))}
    if($Suite -eq 'Grip'){$inputs.Add((Join-Path $work 'src/interaction/TrackedRig.cpp'))}
    if($Suite -eq 'Bc2PhysicalFeed'){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2PhysicalFeed.cpp'))}
    if($Suite -eq 'Bc2PhysicalPistolSlide'){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2PhysicalPistolSlide.cpp'))}
    $arguments+=$inputs.ToArray()+@('/Fe:check.exe','/link')
    foreach($library in @('FvrCore.lib','BC2Camera.lib','BC2Profile.lib','FrostbiteDiscovery.lib','FrostbiteCamera.lib')){
        $arguments+=(Join-Path $canonical ('build/'+$Architecture+'/'+$library))
    }
}
$identities=@($inputs|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}})
$dependencyNames=@()
if($Suite -in @('FeedMechanism','FeedReload','FeedInteraction','FeedReloadSession','FeedAmmunition','FeedEquipmentSession','Bc2FeedEquipmentAdapter','Bc2FeedEquipmentProfile','Bc2PhysicalFeed','Bc2FeedPresentation')) {
    $dependencyNames=@('include/fvr/interaction/FeedMechanism.h','include/fvr/interaction/FeedReload.h','include/fvr/math/StereoMath.h','tests/Test.h')
    if($Suite -in @('FeedInteraction','FeedReloadSession','FeedEquipmentSession')){$dependencyNames+='include/fvr/interaction/FeedInteraction.h'}
    if($Suite -in @('FeedReloadSession','FeedEquipmentSession')){$dependencyNames+='include/fvr/interaction/FeedReloadSession.h'}
    if($Suite -in @('FeedAmmunition','FeedEquipmentSession')){$dependencyNames+='include/fvr/interaction/FeedAmmunition.h'}
    if($Suite -eq 'FeedEquipmentSession'){$dependencyNames+='include/fvr/interaction/FeedEquipmentSession.h'}
    if($Suite -like 'Bc2*') {
        $dependencyNames+=@('include/fvr/interaction/FeedInteraction.h','include/fvr/interaction/FeedReloadSession.h','include/fvr/interaction/FeedAmmunition.h','include/fvr/interaction/FeedEquipmentSession.h','include/fvr/interaction/AmmunitionLedger.h','include/fvr/interaction/HandInteraction.h','src/games/bc2/Bc2FeedEquipmentAdapter.h','src/games/bc2/Bc2FeedEquipmentProfile.h','src/games/bc2/Bc2PhysicalFeed.h')
        if($Suite -eq 'Bc2FeedPresentation'){$dependencyNames+='src/games/bc2/Bc2FeedPresentation.h'}
    }
}
$dependencies=@($dependencyNames|ForEach-Object {$path=Join-Path $work $_;@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}})
if($Suite -eq 'PhysicalPistolSlide') {
    $dependencies=@('include/fvr/interaction/PhysicalPistolSlide.h','include/fvr/interaction/WeaponMechanism.h','include/fvr/interaction/HandInteraction.h','tests/Test.h'|ForEach-Object {$path=Join-Path $work $_;@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}})
}
if($Suite -in @('PistolAfterFeedPolicy','Bc2PistolSlideProfile','Bc2PistolSlidePresentation','Bc2PistolAfterFeedEvidence','Bc2PistolAfterFeedInput','Bc2PhysicalPistolSlide')) {
    $names=@('include/fvr/interaction/PhysicalPistolSlide.h','include/fvr/interaction/PistolAfterFeedPolicy.h','include/fvr/interaction/WeaponMechanism.h','include/fvr/interaction/HandInteraction.h','include/fvr/interaction/AmmunitionLedger.h','include/fvr/interaction/AmmunitionInventory.h','include/fvr/interaction/FeedInteraction.h','tests/Test.h')
    if($Suite -like 'Bc2*'){$names+=@('src/games/bc2/Bc2PistolSlideProfile.h','src/games/bc2/Bc2PistolSlidePresentation.h','src/games/bc2/Bc2PistolAfterFeedEvidence.h','src/games/bc2/Bc2PistolAfterFeedInput.h')}
    if($Suite -eq 'Bc2PhysicalPistolSlide'){$names+='src/games/bc2/Bc2PhysicalPistolSlide.h'}
    $dependencies=@($names|ForEach-Object {$path=Join-Path $work $_;@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}})
}
SaveJson 'command.json' @{compiler=(Get-Command cl.exe).Source;arguments=$arguments;inputs=$identities;dependencies=$dependencies;unchanged_baseline='7063c0003fa5e41027bf831e3bb9c70093962da8'}
Push-Location $output
try {
    & cl.exe @arguments *> compile.log
    if($LASTEXITCODE -ne 0){Get-Content compile.log -Tail 25;throw "Compile failed: $LASTEXITCODE"}
    if($Suite -notin @('GameplaySyntax','NativeSyntax','ReloadSyntax','RigSyntax')){
        & .\check.exe *> test.log
        if($LASTEXITCODE -ne 0){Get-Content test.log -Tail 25;throw "Test failed: $LASTEXITCODE"}
        Get-Content test.log
    }
    SaveJson 'result.json' @{suite=$Suite;architecture=$Architecture;passed=$true;utc=[DateTime]::UtcNow.ToString('o');full_normal_build=$false;installed=$false}
} catch { SaveJson 'failure.json' @{suite=$Suite;architecture=$Architecture;error=$_.Exception.Message};throw }
finally {Pop-Location}

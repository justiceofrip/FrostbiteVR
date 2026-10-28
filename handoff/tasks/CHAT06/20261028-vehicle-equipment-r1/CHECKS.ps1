[CmdletBinding()]
param([ValidatePattern('^[a-zA-Z0-9-]+$')][string]$Run='checks01',
    [ValidateSet('Bc2AmmoResourceService','Bc2NativeCycleService')]
    [string[]]$Tests=@('Bc2AmmoResourceService','Bc2NativeCycleService'))
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if((Split-Path -Leaf (Split-Path -Parent $repository)) -ne 'bc2vr-task-worktrees'){
    throw 'Worker-only check: run from its isolated bc2vr-task-worktrees checkout. Integrator uses normal builds.'
}
$workspace=Split-Path -Parent (Split-Path -Parent $repository)
$canonical=Join-Path $workspace 'FrostbiteVR'
$output=Join-Path $repository "test-temp/chat06/$Run"
if(Test-Path -LiteralPath $output){throw "Retain previous evidence; choose a new Run: $output"}
. (Join-Path $repository 'tools/StorageBudget.ps1')
# Include the canonical/recovery budgets even from a nested isolated worktree.
$roots=@('build','reports','dist','release','test-temp') | ForEach-Object {Join-Path $canonical $_}
$roots+=Join-Path $workspace 'bc2vr-recovery'
$roots+=Join-Path $workspace 'bc2vr-task-worktrees'
$roots+=@(Get-ChildItem -LiteralPath $workspace -Directory -Filter 'bc2vr-preview-*' | ForEach-Object {$_.FullName})
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output -Operation Build -PlannedBytes 256MB -AdditionalRoots $roots
$null=[IO.Directory]::CreateDirectory($output)
function SaveText([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
SaveText (Join-Path $output 'storage.json') ($admission | ConvertTo-Json -Depth 10)
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools are required.'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler=(Get-Command cl.exe -ErrorAction Stop).Source
$includes=@('include','include/fvr/interaction','include/fvr/math','src/games/bc2','tests') | ForEach-Object {"/I$(Join-Path $repository $_)"}
$cycleSources=@('src/games/bc2/Bc2NativeCycleService.cpp','src/games/bc2/Bc2NativeCycleEvidence.cpp',
    'src/games/bc2/Bc2PhysicalPump.cpp','src/games/bc2/Bc2PumpPart.cpp','src/games/bc2/Bc2ReloadHold.cpp',
    'src/games/bc2/Bc2ReloadFlow.cpp','src/games/bc2/Bc2ReloadPresentation.cpp','src/games/bc2/Bc2Rig.cpp',
    'src/games/bc2/Bc2MagazineNativeRegistry.cpp','src/games/bc2/Bc2ReloadState.cpp',
    'src/engines/frostbite/BindingValidation.cpp','src/engines/frostbite/PeImage.cpp',
    'src/interaction/TrackingMath.cpp','src/interaction/SupportGrip.cpp','src/interaction/RigPose.cpp',
    'src/interaction/ControllerInput.cpp','src/interaction/ActionPolicy.cpp','src/interaction/ComfortControls.cpp',
    'src/math/StereoMath.cpp','src/math/WeaponPoseMath.cpp')
$results=@()
foreach($name in $Tests){
    $directory=Join-Path $output $name
    $null=[IO.Directory]::CreateDirectory($directory)
    $sources=@("tests/${name}Tests.cpp")
    if($name -eq 'Bc2NativeCycleService'){$sources+=$cycleSources}
    $sources=@($sources | ForEach-Object {Join-Path $repository $_})
    $exe=Join-Path $directory "${name}Tests.exe"
    $arguments=@('/nologo','/std:c++20','/EHsc','/MT','/O1','/Gy','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN')+$includes+$sources+@("/Fo$directory\","/Fe$exe",'/link','/OPT:REF')
    SaveText (Join-Path $directory 'arguments.json') ($arguments | ConvertTo-Json)
    & $compiler @arguments *> (Join-Path $directory 'build.log')
    $buildExit=$LASTEXITCODE;$testExit=$null
    if($buildExit -eq 0){
        & $exe *> (Join-Path $directory 'test.log')
        $testExit=$LASTEXITCODE
        Get-Content -LiteralPath (Join-Path $directory 'test.log') -Tail 10
    }else{Get-Content -LiteralPath (Join-Path $directory 'build.log') -Tail 25}
    $results+=[ordered]@{test=$name;build_exit=$buildExit;test_exit=$testExit;sources=$sources}
}
SaveText (Join-Path $output 'results.json') ([ordered]@{
    evidence='Isolated x64 CPU tests; synthetic native callbacks, no game/headset acceptance'
    compiler=$compiler;results=$results;normal_x86_x64_builds='Pending integrator'
} | ConvertTo-Json -Depth 8)
if(@($results | Where-Object {$_.build_exit -ne 0 -or $_.test_exit -ne 0}).Count){throw "Focused checks failed; retain $output"}
Write-Output "Focused checks passed: $output"

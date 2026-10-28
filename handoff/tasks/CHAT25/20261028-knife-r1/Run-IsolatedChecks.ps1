[CmdletBinding()]
param(
    [ValidateSet('x86','x64')][string]$Architecture='x86',
    [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$Attempt='final01',
    [ValidateSet('MeleeStroke','SelectedMeleeAttack','KnifeMotionBoundary')]
    [string[]]$Checks=@('MeleeStroke','SelectedMeleeAttack','KnifeMotionBoundary')
)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=Join-Path $repository "reports/chat25-chat26-20261028-knife-r1/$Architecture-$Attempt"
if(Test-Path -LiteralPath $output){throw 'This attempt already exists. Select a new -Attempt to preserve prior evidence.'}
. (Join-Path $repository 'tools/StorageBudget.ps1')
# Include the canonical project's retained output in this isolated admission.
# Its source/build trees are never configured, linked against, or written here.
$countedRoots=@(Get-FvrStorageRoots -RepositoryRoot $canonical)
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output `
    -Operation Build -PlannedBytes 128MB -AdditionalRoots $countedRoots
$null=[IO.Directory]::CreateDirectory($output)
function Save-Json([string]$Name,$Value){
    [IO.File]::WriteAllText((Join-Path $output $Name),($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
Save-Json 'storage-admission.json' $admission
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
$vsArch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $vsArch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler=(Get-Command cl.exe -ErrorAction Stop).Source
$flags=@('/nologo','/std:c++20','/EHsc','/W4','/permissive-','/MT','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
    "/I$repository/include","/I$repository/include/fvr/interaction","/I$repository/include/fvr/math",
    "/I$repository/tests","/I$repository/src/games/bc2","/I$repository/src/platform/windows")
$results=[Collections.Generic.List[object]]::new()
Push-Location $output
try {
    foreach($name in $Checks){
        $source=Join-Path $repository "tests/${name}Tests.cpp"
        $exe=Join-Path $output "${name}Tests.exe"
        $log=Join-Path $output "$name-build.log"
        $sources=@($source)
        if($name -eq 'KnifeMotionBoundary'){
            $sources+=@('src/math/StereoMath.cpp','src/math/WeaponPoseMath.cpp','src/interaction/TrackingMath.cpp',
                'src/games/bc2/Bc2ReloadFlow.cpp','src/games/bc2/Bc2ReloadState.cpp',
                'src/engines/frostbite/BindingValidation.cpp' |
                ForEach-Object {Join-Path $repository $_})
        }
        & $compiler @flags @sources "/Fe$exe" *> $log
        $buildExit=$LASTEXITCODE
        if($buildExit -ne 0){
            $results.Add([pscustomobject]@{test=$name;build_exit=$buildExit;test_exit=$null})
            Get-Content -LiteralPath $log -Tail 50;throw "$name compile failed: $buildExit"
        }
        & $exe *> (Join-Path $output "$name-test.log")
        $testExit=$LASTEXITCODE
        $results.Add([pscustomobject]@{test=$name;build_exit=$buildExit;test_exit=$testExit})
        if($testExit -ne 0){Get-Content -LiteralPath (Join-Path $output "$name-test.log");throw "$name failed: $testExit"}
        Write-Output "$name PASS ($Architecture)"
    }
    # Compile the changed native translation unit with its real x86 feature gate.
    # This is a compile check only; the integrator owns normal builds/linking.
    $native=Join-Path $repository 'src/games/bc2/Bc2SelectedKnifeNative.cpp'
    $minhook=Join-Path $repository 'third_party/minhook-1.3.4/include'
    & $compiler @flags "/I$minhook" '/DFVR_BC2_CHEST_KNIFE_DRAW_RETURN' '/c' $native `
        "/Fo$(Join-Path $output 'Bc2SelectedKnifeNative.obj')" *> (Join-Path $output 'native-compile.log')
    $nativeExit=$LASTEXITCODE
    $results.Add([pscustomobject]@{test='Bc2SelectedKnifeNative compile only';build_exit=$nativeExit;test_exit=$null})
    if($nativeExit -ne 0){Get-Content -LiteralPath (Join-Path $output 'native-compile.log') -Tail 50;throw "Native compile failed: $nativeExit"}
    Write-Output "Bc2SelectedKnifeNative compile PASS ($Architecture)"
} finally {
    Pop-Location
    $paths=@('include/fvr/interaction/MeleeStroke.h','include/fvr/interaction/SelectedMeleeAttack.h',
        'src/games/bc2/Bc2KnifeSoundObservation.h','src/games/bc2/Bc2SelectedKnifeNative.cpp',
        'tests/MeleeStrokeTests.cpp','tests/SelectedMeleeAttackTests.cpp','tests/KnifeMotionBoundaryTests.cpp')
    $hashes=@(foreach($path in $paths){[pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath (Join-Path $repository $path) -Algorithm SHA256).Hash.ToLowerInvariant()}})
    Save-Json 'results.json' ([pscustomobject]@{evidence='isolated deterministic and compile checks only';architecture=$Architecture;compiler=$compiler;results=$results.ToArray();sources=$hashes})
}

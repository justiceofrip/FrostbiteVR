[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('x86','x64')][string]$Architecture,
      [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=[IO.Path]::GetFullPath($OutputPath)
$reportRoot=Join-Path $work 'reports/coordinator-20261030-pipeline34'
if(-not $output.StartsWith($reportRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Output outside report root'}
if(Test-Path -LiteralPath $output){throw 'Use a fresh output folder; retain failed evidence'}
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 128MB -RawCaptureBytes 0 -AdditionalRoots @((Join-Path $canonical 'build'),(Join-Path $canonical 'reports'),'G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$null=New-Item -ItemType Directory -Path $output
function SaveJson($Name,$Value){[IO.File]::WriteAllText((Join-Path $output $Name),($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
SaveJson 'storage.json' $budget
$env:TEMP=Join-Path $reportRoot 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$null=New-Item -ItemType Directory -Path $env:TEMP -Force
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler=(Get-Command cl.exe).Source
if((Split-Path -Leaf (Split-Path -Parent $compiler)) -ne $Architecture){throw 'Wrong compiler architecture'}
$profiles=Join-Path $work 'profiles/mechanism-family41'
$common=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/UNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN','/showIncludes',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'tests')),('/I'+$profiles),
    '/DFVR_BC2_FEED_REGISTRY_HEADER="Feed.generated.h"','/DFVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER="Slide.generated.h"',
    '/DFVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="CombinedRegistry.h"','/DFVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER="CombinedGeometry.h"')
$registrySources=@('Bc2FeedRegistry.cpp','Bc2PistolSlideRegistry.cpp','Bc2MagazineNativeRegistry.cpp','Bc2MagazineGeometryProfile.cpp')|ForEach-Object {Join-Path $work ('src/games/bc2/'+$_)}
$libraries=@('FvrCore.lib','BC2Camera.lib','BC2Profile.lib','FrostbiteDiscovery.lib','FrostbiteCamera.lib','FvrD3D11RigidProp.lib','FvrRigidPropGeometry.lib')|ForEach-Object {Join-Path $canonical ('build/'+$Architecture+'/'+$_)}
$libraries+=@('d3d11.lib','d3dcompiler.lib')
$cache=Join-Path $work 'reports/coordinator-20261030-family-enrollment41/body-ammo.fvrprop'
if((Get-FileHash -LiteralPath $cache).Hash.ToLowerInvariant() -ne 'b88c531d15ff2aa43d5fa3ea13984941b840932c47822d4d6b93f88460fa7d41'){throw 'Cache identity changed'}
Push-Location $output
try {
    foreach($suite in @('FamilyEnrollment','BodyAmmoCache')){
        $sources=$registrySources
        if($suite -eq 'FamilyEnrollment'){$sources+=Join-Path $PSScriptRoot 'FamilyEnrollmentConsumer.cpp'}
        else {$sources+=@('tests/Bc2BodyAmmoCacheTests.cpp','src/games/bc2/Bc2BodyAmmoRenderer.cpp','src/games/bc2/Bc2BodyAmmoGeometryCache.cpp')|ForEach-Object {Join-Path $work $_}}
        $arguments=$common+$sources+@(('/Fe:'+$suite+'.exe'),'/link')+$libraries
        SaveJson ($suite+'-command.json') @{compiler=$compiler;arguments=$arguments;inputs=@($sources|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}});baseline='7063c0003fa5e41027bf831e3bb9c70093962da8'}
        & cl.exe @arguments *> ($suite+'-compile.log')
        if($LASTEXITCODE -ne 0){Get-Content ($suite+'-compile.log') -Tail 30;throw "$suite compile failed: $LASTEXITCODE"}
        $headers=@(Get-Content ($suite+'-compile.log')|ForEach-Object {if($_ -match '^Note: including file:\s+(.+)$'){$Matches[1].Trim()}}|Where-Object {$_ -like ($work+'*')}|Sort-Object -Unique)
        SaveJson ($suite+'-dependencies.json') @($headers|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}})
        $testArguments=if($suite -eq 'BodyAmmoCache'){@($cache,'57')}else{@()}
        & (Join-Path $output ($suite+'.exe')) @testArguments *> ($suite+'-test.log')
        if($LASTEXITCODE -ne 0){Get-Content ($suite+'-test.log') -Tail 30;throw "$suite check failed: $LASTEXITCODE"}
        Get-Content ($suite+'-test.log')
        SaveJson ($suite+'-result.json') @{passed=$true;architecture=$Architecture;suite=$suite;utc=[DateTime]::UtcNow.ToString('o');full_normal_build=$false;installed=$false;actual_game=$false;headset=$false}
    }
} catch {SaveJson 'failure.json' @{error=$_.Exception.Message};throw}
finally {Pop-Location}

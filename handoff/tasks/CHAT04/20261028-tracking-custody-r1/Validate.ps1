[CmdletBinding()]
param([string[]]$Tests=@('Bc2MagazineResource','DetachableMagazine','Bc2MagazinePhysical',
    'Bc2PhysicalPumpRecovery','Bc2PhysicalBoltRecovery','Bc2BoltPlayerCustody',
    'M95BoltCustody','SupportOwnershipRecovery','HandInteraction','AmmoSupply','Bc2AmmoResourceService'))
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$output=Join-Path $repository 'test-temp/chat04-portable'
. (Join-Path $repository 'tools/StorageBudget.ps1')
# Include canonical generated roots explicitly: the worker has a different parent.
$canonical=Join-Path ([IO.Path]::GetFullPath((Join-Path $repository '../..'))) 'FrostbiteVR'
$additional=@(Get-FvrStorageRoots -RepositoryRoot $canonical)
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output -Operation Build -PlannedBytes 512MB -AdditionalRoots $additional
New-Item -ItemType Directory -Path $output -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'storage.json'),($admission|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
$scratch=Join-Path $output 'scratch'
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$env:TEMP=$scratch;$env:TMP=$scratch;$env:TMPDIR=$scratch
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio C++ tools unavailable'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
# Standalone deterministic harness, with no native DLL, XR, game or shared build targets.
# Reuse only the portable library source declarations, never the normal CMake project.
$source=[IO.File]::ReadAllText((Join-Path $repository 'CMakeLists.txt'))
$files=[Collections.Generic.HashSet[string]]::new()
foreach($library in @('FvrCore','FrostbiteDiscovery','FrostbiteCamera','BC2Profile','BC2Camera')){
    $pattern='(?s)(?:add_library|target_sources)\('+[regex]::Escape($library)+'\s+[^)]*\)'
    foreach($declaration in [regex]::Matches($source,$pattern)){
        foreach($file in [regex]::Matches($declaration.Value,'src/[A-Za-z0-9_./-]+\.cpp')){[void]$files.Add($file.Value)}
    }
}
if($files.Count -lt 20 -or $files.Count -gt 160){throw 'Unexpected portable source manifest'}
$root=$repository.Replace('\','/')
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('cmake_minimum_required(VERSION 3.24)')
$lines.Add('project(CHAT04Portable LANGUAGES CXX)')
$lines.Add('set(CMAKE_CXX_STANDARD 20)')
$lines.Add('set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)')
$lines.Add('enable_testing()')
$lines.Add('add_library(TaskPortable STATIC')
foreach($file in ($files|Sort-Object)){$lines.Add('  "'+$root+'/'+$file+'"')}
$lines.Add(')')
$lines.Add('target_include_directories(TaskPortable PUBLIC "'+$root+'/include" "'+$root+'/include/fvr/interaction" "'+$root+'/include/fvr/math" "'+$root+'/src/games/bc2" "'+$root+'/src/engines/frostbite")')
foreach($test in $Tests){
    if($test -notmatch '^[A-Za-z0-9]+$' -or !(Test-Path -LiteralPath (Join-Path $repository "tests/${test}Tests.cpp"))){throw "Invalid test: $test"}
    $lines.Add('add_executable('+ $test +'Tests "'+$root+'/tests/'+$test+'Tests.cpp")')
    $lines.Add('target_link_libraries('+ $test +'Tests PRIVATE TaskPortable)')
    $lines.Add('add_test(NAME '+$test+' COMMAND '+$test+'Tests)')
}
[IO.File]::WriteAllLines((Join-Path $output 'CMakeLists.txt'),$lines,[Text.UTF8Encoding]::new($false))
function Step([string]$Name,[string]$Exe,[string[]]$Arguments){
    & $Exe @Arguments *> (Join-Path $output "$Name.log")
    $result=$LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $output "$Name.log") -Tail 16
    if($result -ne 0){throw "$Name failed ($result); retained log in $output"}
}
Step 'configure' 'cmake' @('-S',$output,'-B',(Join-Path $output 'objects'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release')
Step 'compile' 'cmake' @('--build',(Join-Path $output 'objects'),'--parallel','4')
Step 'tests' 'ctest' @('--test-dir',(Join-Path $output 'objects'),'--output-on-failure')

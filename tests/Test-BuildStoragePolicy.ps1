[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$repository=Split-Path -Parent $PSScriptRoot
. (Join-Path $repository 'tools/StorageBudget.ps1')
$testRoot=[IO.Path]::GetFullPath((Join-Path $repository 'test-temp'))
$fixture=Join-Path $testRoot ('build-storage-policy-'+[guid]::NewGuid().ToString('N'))
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $fixture -Operation Build -PlannedBytes 10MB
$previousTemp=$env:TEMP
$previousTmp=$env:TMP
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw "FAIL: $Name"};$checks.Add($Name)}
function Invoke-Native([string]$Name,[string]$Program,[string[]]$Arguments){
    $output=& $Program @Arguments 2>&1
    $code=$LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture "$Name.log")
    if($code -ne 0){throw "$Name failed ($code): $($output -join '`n')"}
    return ($output -join "`n")
}
try {
    $scratch=Join-Path $fixture 'scratch'
    New-Item -ItemType Directory -Path $scratch -Force | Out-Null
    $env:TEMP=$scratch
    $env:TMP=$scratch
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
    & (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch x64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $cmake=(Get-Command cmake -ErrorAction Stop).Source
    $policySource=Get-Content -LiteralPath (Join-Path $repository 'CMakeLists.txt') -Raw
    $marker='# Hundreds of small regression executables link the same native/core libraries.'
    $policyOffset=$policySource.IndexOf($marker,[StringComparison]::Ordinal)
    Check ($policyOffset -ge 0) 'fixture uses the exact repository policy block'
    $policy=$policySource.Substring($policyOffset)
    @'
cmake_minimum_required(VERSION 3.24)
project(BoundedBuildPolicy LANGUAGES CXX)
enable_testing()
add_library(NativeSymbolFixture SHARED native.cpp)
add_executable(ToolSymbolFixture main.cpp)
add_executable(BoundedPolicyTests main.cpp)
add_test(NAME NativeToolAlsoUsedByCTest COMMAND ToolSymbolFixture)
add_test(NAME UnitFixture COMMAND BoundedPolicyTests)
'@ + "`n" + $policy | Set-Content -LiteralPath (Join-Path $fixture 'CMakeLists.txt')
    'extern "C" __declspec(dllexport) int fixture_value() { return 42; }' | Set-Content -LiteralPath (Join-Path $fixture 'native.cpp')
    'int main() { return 0; }' | Set-Content -LiteralPath (Join-Path $fixture 'main.cpp')
    $build=Join-Path $fixture 'build'
    $symbols=Join-Path $build 'symbols'
    foreach($case in @(
        @{Name='normal';Config='RelWithDebInfo';TestSymbols='OFF';ExpectedTestPdb=$false},
        @{Name='explicit-test-symbols';Config='RelWithDebInfo';TestSymbols='ON';ExpectedTestPdb=$true},
        @{Name='debug';Config='Debug';TestSymbols='OFF';ExpectedTestPdb=$true}
    )){
        if($case.Name -eq 'debug'){
            # The configuration switch relinks this target. Remove its previous
            # PDB so the Debug assertion cannot pass using the opt-in artifact.
            $pdb=Join-Path $symbols 'BoundedPolicyTests.pdb'
            if(Test-Path -LiteralPath $pdb){Remove-Item -LiteralPath $pdb -Force}
        }
        $configure=@('-S',$fixture,'-B',$build,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$($case.Config)","-DFVR_TEST_DEBUG_SYMBOLS=$($case.TestSymbols)","-DCMAKE_PDB_OUTPUT_DIRECTORY=$symbols","-DCMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO=$symbols")
        $null=Invoke-Native "$($case.Name)-configure" $cmake $configure
        $null=Invoke-Native "$($case.Name)-build" $cmake @('--build',$build,'--parallel','1')
        Check (Test-Path -LiteralPath (Join-Path $symbols 'NativeSymbolFixture.pdb')) "$($case.Name): native DLL retains linker symbols"
        Check (Test-Path -LiteralPath (Join-Path $symbols 'ToolSymbolFixture.pdb')) "$($case.Name): tool retains linker symbols even when registered with CTest"
        Check ((Test-Path -LiteralPath (Join-Path $symbols 'BoundedPolicyTests.pdb')) -eq $case.ExpectedTestPdb) "$($case.Name): test linker PDB matches requested policy"
        $ninja=Get-Content -LiteralPath (Join-Path $build 'build.ninja') -Raw
        Check (-not $ninja.Contains('FrostbiteVR-checkpoint361-symbols')) "$($case.Name): no inherited historical PDB path"
        if($case.Name -eq 'normal'){
            $dryRun=Invoke-Native 'normal-no-relink' $cmake @('--build',$build,'--','-n')
            Check ($dryRun -match 'no work to do') 'omitted test PDB does not cause repeated relinking'
        }
        $usage=Get-FvrStorageUsage -Roots @($fixture)
        Check ($usage.logical_bytes -lt 10MB) "$($case.Name): complete fixture remains below 10 MiB"
    }
    [pscustomobject]@{passed=$true;checks=$checks.Count;details=$checks.ToArray();fixture_logical_bytes=$usage.logical_bytes;fixture_allocated_bytes=$usage.allocated_bytes;policy_sha256=([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($policy))).ToLowerInvariant());admission=$admission;native_game_or_headset_test=$false} | ConvertTo-Json -Depth 8
} finally {
    $env:TEMP=$previousTemp
    $env:TMP=$previousTmp
    # Only this script's unique, regenerable fixture is removed. Verify the
    # absolute parent and prefix before the recursive filesystem operation.
    $resolved=[IO.Path]::GetFullPath($fixture)
    if((Split-Path -Parent $resolved) -ne $testRoot -or (Split-Path -Leaf $resolved) -notlike 'build-storage-policy-*'){
        throw "Refusing fixture cleanup outside the expected test directory: $resolved"
    }
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
